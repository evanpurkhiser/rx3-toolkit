from __future__ import annotations

import unittest
from argparse import Namespace
from unittest.mock import patch

from tools.rx3_remote.client import Connection, RemoteError
from tools.rx3_remote.controls import CONTROLS, resolve_control
from tools.rx3_remote.generate_header import OUTPUT, render
from tools.rx3_remote import cli
from tools.rx3_remote.protocol import (
    ACK, CAP_COMMANDS, CAP_EVENTS, CAP_SCHEMA, COMMAND, ERROR, EVENT, HELLO,
    SCHEMA, ControlEvent, ControlTuple, Decoder, ERROR_INVALID_COMMAND,
    Hello, Message, OP_ABSOLUTE_MOVED, OP_PRESSED, OP_RELEASED, ProtocolError,
    SOURCE_PHYSICAL, decode_ack, encode_ack,
    encode_error, encode_message, encode_schema,
)


class RemoteProtocolTests(unittest.TestCase):
    def test_control_catalog_and_generated_header_are_current(self) -> None:
        self.assertEqual(len({control.name for control in CONTROLS}), len(CONTROLS))
        self.assertEqual(len({control.key_code for control in CONTROLS}), len(CONTROLS))
        self.assertEqual(OUTPUT.read_text(), render())

    def test_catalog_covers_recovered_physical_key_table(self) -> None:
        expected = {
            0x0401, 0x0402, 0x0493, 0x0814, 0x0815, 0x0816,
            *range(0x4101, 0x4105), *range(0x4107, 0x4122),
            0x4124, 0x4125, 0x4126, *range(0x420C, 0x4210),
            0x4212, 0x4214, 0x4215, 0x4305, 0x4306, 0x4311,
            0x4322, 0x4323, *range(0x4403, 0x440B),
            *range(0x448B, 0x4493), *range(0x5019, 0x501D),
            0x501E, 0x501F, 0x5020, 0x509D, *range(0x50A1, 0x50A8),
            0x6017, 0x6018, 0x8001, 0x8002,
        }
        self.assertTrue(expected.issubset({control.key_code for control in CONTROLS}))

    def test_press_convenience_emits_press_then_release(self) -> None:
        args = Namespace(control="deck.play_pause", channel=2)
        with patch.object(cli, "send_commands") as send:
            cli.press(args)
        operations = [command.operation for command in send.call_args.args[1]]
        self.assertEqual(operations, [OP_PRESSED, OP_RELEASED])
        self.assertEqual(send.call_args.kwargs["inter_command_delay"], 0.03)

    def test_set_convenience_derives_normalized_float(self) -> None:
        args = Namespace(control="mixer.crossfader", channel=0, value=512)
        with patch.object(cli, "send_commands") as send:
            cli.set_absolute(args)
        command = send.call_args.args[1][0]
        self.assertEqual(command.operation, OP_ABSOLUTE_MOVED)
        self.assertEqual(command.value, 512)
        self.assertAlmostEqual(command.float_value, 512 / 1023, places=6)

    def test_decoder_accepts_arbitrary_chunks_and_multiple_frames(self) -> None:
        hello = Hello(b"\xcf\x30\x92\x38", 1, 19, 1, 7)
        encoded = encode_message(HELLO, hello.encode()) + encode_message(
            SCHEMA, encode_schema(0x12345678, len(CONTROLS))
        )
        decoder = Decoder()
        messages: list[Message] = []
        for offset in range(0, len(encoded), 3):
            messages.extend(decoder.feed(encoded[offset:offset + 3]))

        self.assertEqual([message.type for message in messages], [HELLO, SCHEMA])
        self.assertEqual(Hello.decode(messages[0].payload), hello)

    def test_command_round_trips_exact_send_key_tuple(self) -> None:
        command = ControlTuple.create(
            "mixer.crossfader", operation=5, channel=0,
            value=464, float_value=464 / 1023, auxiliary=-4,
        )
        decoded = ControlTuple.decode(command.encode())
        self.assertEqual(decoded.key_code, 0x6017)
        self.assertEqual(decoded.name, "mixer.crossfader")
        self.assertEqual(decoded.operation, 5)
        self.assertEqual(decoded.value, 464)
        self.assertAlmostEqual(decoded.float_value, 464 / 1023, places=6)
        self.assertEqual(decoded.auxiliary, -4)

    def test_cli_accepts_operation_name_or_numeric_code(self) -> None:
        self.assertEqual(cli.parse_operation("absolute_moved"), OP_ABSOLUTE_MOVED)
        self.assertEqual(cli.parse_operation("0x5"), OP_ABSOLUTE_MOVED)

    def test_event_has_source_flags_and_device_timestamp(self) -> None:
        control = ControlTuple(0x4101, 2, 1, 1, 0, 0)
        expected = ControlEvent(control, SOURCE_PHYSICAL, 3, 99887766)
        self.assertEqual(ControlEvent.decode(expected.encode()), expected)

    def test_unknown_event_code_keeps_numeric_identity(self) -> None:
        control = ControlTuple(0xDEAD, 1, 0)
        self.assertEqual(control.name, "key_0xdead")
        with self.assertRaises(KeyError):
            resolve_control(0xDEAD)

    def test_command_requires_request_id(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "nonzero request ID"):
            encode_message(COMMAND, ControlTuple(0x4101, 2, 1).encode())

    def test_decoder_rejects_oversized_declared_payload(self) -> None:
        valid = bytearray(encode_message(HELLO))
        valid[12:16] = (4097).to_bytes(4, "big")
        with self.assertRaisesRegex(ProtocolError, "too large"):
            list(Decoder().feed(valid))


class RemoteClientTests(unittest.TestCase):
    class FakeSocket:
        def __init__(self, incoming: list[bytes], response_type: int, response: bytes):
            self.incoming = incoming
            self.response_type = response_type
            self.response = response
            self.sent: list[Message] = []

        def sendall(self, data: bytes) -> None:
            messages = list(Decoder().feed(data))
            self.sent.extend(messages)
            message = messages[0]
            if message.type == COMMAND:
                self.incoming.append(encode_message(
                    self.response_type, self.response, message.request_id
                ))

        def recv(self, _size: int) -> bytes:
            return self.incoming.pop(0) if self.incoming else b""

        def close(self) -> None:
            pass

    def make_client(self, response_type: int, response: bytes) -> tuple[Connection, FakeSocket]:
        hello = Hello(
            b"\xcf\x30\x92\x38", 1, 19, 1,
            CAP_EVENTS | CAP_COMMANDS | CAP_SCHEMA,
        )
        incoming = [
            encode_message(HELLO, hello.encode()),
            encode_message(SCHEMA, encode_schema(0xAABBCCDD, len(CONTROLS))),
        ]
        fake = self.FakeSocket(incoming, response_type, response)
        return Connection(fake), fake  # type: ignore[arg-type]

    def test_handshake_and_command_ack(self) -> None:
        client, fake = self.make_client(ACK, encode_ack(17))
        handshake = client.handshake()
        self.assertEqual(handshake.hello.firmware_minor, 19)
        self.assertEqual(handshake.schema_crc32, 0xAABBCCDD)
        result = client.command(ControlTuple(0x4102, 2, 1), 99)
        self.assertEqual(result, 17)
        self.assertEqual(fake.sent[0].type, COMMAND)
        self.assertEqual(ControlTuple.decode(fake.sent[0].payload).name, "deck.cue")

    def test_command_error_preserves_numeric_detail(self) -> None:
        client, _ = self.make_client(
            ERROR, encode_error(ERROR_INVALID_COMMAND, 0x4101)
        )
        client.handshake()
        with self.assertRaises(RemoteError) as caught:
            client.command(ControlTuple(0x4101, 15, 1), 7)
        self.assertEqual(caught.exception.code, ERROR_INVALID_COMMAND)
        self.assertEqual(caught.exception.detail, 0x4101)

    def test_command_defers_event_that_arrives_before_ack(self) -> None:
        client, fake = self.make_client(ACK, encode_ack(0))
        client.handshake()
        event = ControlEvent(
            ControlTuple(0x4101, OP_PRESSED, 1), SOURCE_PHYSICAL, 0, 123
        )
        fake.incoming.append(encode_message(EVENT, event.encode()))

        self.assertEqual(client.command(ControlTuple(0x4102, 2, 1), 99), 0)
        self.assertEqual(next(client.events()), event)


if __name__ == "__main__":
    unittest.main()
