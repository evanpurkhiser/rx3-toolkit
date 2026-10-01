"""Synchronous client primitives for RX3R over USB Ethernet."""

from __future__ import annotations

from dataclasses import dataclass
import socket
import time
from typing import Iterator

from .protocol import (
    ACK, COMMAND, ERROR, EVENT, HELLO, PING, PONG, SCHEMA, SUBSCRIBE,
    ControlEvent, ControlTuple, Decoder, ERROR_NAMES, EVENT_ALL, Hello, Message,
    ProtocolError, decode_ack, decode_error, decode_schema, encode_message,
    encode_subscribe,
)


DEFAULT_HOST = "169.254.100.2"
DEFAULT_PORT = 7357


class RemoteError(RuntimeError):
    """The RX3 rejected a command."""

    def __init__(self, code: int, detail: int) -> None:
        self.code = code
        self.detail = detail
        super().__init__(f"{ERROR_NAMES.get(code, f'error {code}')} (detail {detail})")


@dataclass(frozen=True)
class Handshake:
    hello: Hello
    schema_crc32: int
    control_count: int


class Connection:
    def __init__(self, connection: socket.socket) -> None:
        self.socket = connection
        self.decoder = Decoder()
        self._pending: list[Message] = []
        self._deferred_events: list[Message] = []

    @classmethod
    def connect(cls, host: str, port: int, timeout: float = 5.0) -> Connection:
        connection = socket.create_connection((host, port), timeout=timeout)
        connection.settimeout(timeout)
        return cls(connection)

    def close(self) -> None:
        self.socket.close()

    def __enter__(self) -> Connection:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def send(self, message_type: int, payload: bytes = b"", request_id: int = 0) -> None:
        self.socket.sendall(encode_message(message_type, payload, request_id))

    def receive(self) -> Message:
        while not self._pending:
            data = self.socket.recv(16 * 1024)
            if not data:
                raise ConnectionError("RX3 closed the connection")
            self._pending.extend(self.decoder.feed(data))
        return self._pending.pop(0)

    def handshake(self) -> Handshake:
        hello_message = self.receive()
        if hello_message.type != HELLO or hello_message.request_id:
            raise ProtocolError("connection did not begin with an unsolicited hello")
        hello = Hello.decode(hello_message.payload)

        schema_message = self.receive()
        if schema_message.type != SCHEMA or schema_message.request_id:
            raise ProtocolError("hello was not followed by schema identity")
        schema_crc32, control_count = decode_schema(schema_message.payload)
        return Handshake(hello, schema_crc32, control_count)

    def subscribe(self, event_mask: int = EVENT_ALL) -> None:
        self.send(SUBSCRIBE, encode_subscribe(event_mask))

    def events(self) -> Iterator[ControlEvent]:
        while True:
            message = (
                self._deferred_events.pop(0)
                if self._deferred_events else self.receive()
            )
            if message.type == EVENT:
                yield ControlEvent.decode(message.payload)
            elif message.type == PING:
                self.send(PONG, message.payload)
            elif message.type == ERROR:
                raise RemoteError(*decode_error(message.payload))

    def command(self, command: ControlTuple, request_id: int) -> int:
        if request_id == 0:
            raise ValueError("request ID must be nonzero")
        self.send(COMMAND, command.encode(), request_id)
        while True:
            message = self.receive()
            if message.type == PING:
                self.send(PONG, message.payload)
                continue
            if message.type == EVENT and message.request_id == 0:
                self._deferred_events.append(message)
                continue
            if message.request_id != request_id:
                raise ProtocolError(
                    f"unexpected {message.name} response with request ID "
                    f"{message.request_id}"
                )
            if message.type == ACK:
                return decode_ack(message.payload)
            if message.type == ERROR:
                raise RemoteError(*decode_error(message.payload))
            raise ProtocolError(f"unexpected {message.name} response to command")


def reconnecting_events(
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    *,
    reconnect_delay: float = 1.0,
) -> Iterator[tuple[Handshake, ControlEvent | None]]:
    """Yield a handshake marker and events, reconnecting after transport failure."""
    while True:
        try:
            with Connection.connect(host, port) as connection:
                handshake = connection.handshake()
                connection.subscribe()
                connection.socket.settimeout(None)
                yield handshake, None
                for event in connection.events():
                    yield handshake, event
        except (ConnectionError, OSError, ProtocolError):
            time.sleep(reconnect_delay)
