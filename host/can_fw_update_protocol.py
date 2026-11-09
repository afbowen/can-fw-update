# Auto-generated. Do not edit.
from enum import IntEnum

CMD_ID_BASE = 0x600
DATA_ID_BASE = 0x640
STATUS_ID_BASE = 0x680
BLOCK_SIZE = 224

class Command(IntEnum):
    BEGIN = 0x81
    BLOCK = 0x82
    DATA = 0x83
    END = 0x84
    ABORT = 0x85

class Status(IntEnum):
    OK = 0
    BAD_STATE = 1
    BAD_OFFSET = 2
    FLASH_ERROR = 3
    CRC_ERROR = 4
    BAD_SEQUENCE = 5
