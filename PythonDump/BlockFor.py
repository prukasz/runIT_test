import struct
from typing import Optional, Union, List
from enum import IntEnum

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref
from Mem import mem_context_t


class ForCondition(IntEnum):
    """Loop continuation condition."""
    GT  = 0x01  # > (while iterator > limit)
    LT  = 0x02  # < (while iterator < limit)
    GTE = 0x04  # >= (while iterator >= limit)
    LTE = 0x05  # <= (while iterator <= limit)


_CONDITION_STR_MAP = {
    ">": ForCondition.GT,
    "<": ForCondition.LT,
    ">=": ForCondition.GTE,
    "<=": ForCondition.LTE,
}


def _resolve_condition(val) -> ForCondition:
    """Accept ForCondition enum, string like '<', or None (defaults to LT)."""
    if val is None:
        return ForCondition.LT
    if isinstance(val, ForCondition):
        return val
    if isinstance(val, str):
        if val in _CONDITION_STR_MAP:
            return _CONDITION_STR_MAP[val]
        raise ValueError(f"Unknown condition string '{val}'. Use one of: {list(_CONDITION_STR_MAP.keys())}")
    raise TypeError(f"condition must be ForCondition, str, or None, got {type(val)}")


class ForOperator(IntEnum):
    """Iterator modification operator."""
    ADD = 0x01  # iterator += step
    SUB = 0x02  # iterator -= step
    MUL = 0x03  # iterator *= step
    DIV = 0x04  # iterator /= step


"""Operator string mapping for user-friendly input.
Accepts '+', '-', '*', '/' as operator inputs in BlockFor constructor."""
_OPERATOR_STR_MAP = {
    "+": ForOperator.ADD,
    "-": ForOperator.SUB,
    "*": ForOperator.MUL,
    "/": ForOperator.DIV,
}


def _resolve_operator(val) -> ForOperator:
    """Accept ForOperator enum, string like '+', or None (defaults to ADD)."""
    if val is None:
        return ForOperator.ADD
    if isinstance(val, ForOperator):
        return val
    if isinstance(val, str):
        if val in _OPERATOR_STR_MAP:
            return _OPERATOR_STR_MAP[val]
        raise ValueError(f"Unknown operator string '{val}'. Use one of: {list(_OPERATOR_STR_MAP.keys())}")
    raise TypeError(f"operator must be ForOperator, str, or None, got {type(val)}")


class BlockFor(Block):
    """
    FOR loop block.
    
    Executes a chain of blocks repeatedly based on loop configuration.
    
    Inputs:
        - in_0 (EN): Enable input (Boolean)
        - in_1 (START): Initial iterator value (optional, overrides config)
        - in_2 (LIMIT): Loop end limit (optional, overrides config)
        - in_3 (STEP): Iterator step value (optional, overrides config)
    
    Outputs:
        - q_0 (ENO): Enable output (Boolean)
        - q_1 (ITER): Current iterator value (Float)
    
    Config:
        - chain_len: Number of blocks in the loop body
        - condition: Loop continuation condition (GT, LT, GTE, LTE)
        - operator: Iterator modification operator (ADD, SUB, MUL, DIV)
        - config_start, config_limit, config_step: Default values
    
    Usage:
        block = BlockFor(
            idx=1,
            ctx=my_context,
            chain_len=3,  # 3 blocks in loop body
            start=0.0,
            limit=10.0,
            step=1.0,
            condition=ForCondition.LT,
            operator=ForOperator.ADD,
            en=Ref("enable")
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 chain_len: int,
                 start: Union[float, int, Ref, None] = 0.0,
                 limit: Union[float, int, Ref, None] = 10.0,
                 step: Union[float, int, Ref, None] = 1.0,
                 condition = ForCondition.LT,
                 operator = ForOperator.ADD,
                 en: Optional[Ref] = None):

        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_FOR, ctx=ctx)
        
        self.chain_len = chain_len
        self.condition = _resolve_condition(condition)
        self.operator = _resolve_operator(operator)
         
        # Process parameters
        def process_param(arg, default_val):
            if isinstance(arg, (int, float)):
                return float(arg), None
            elif isinstance(arg, Ref):
                return default_val, arg
            return default_val, None
        
        self.config_start, start_ref = process_param(start, 0.0)
        self.config_limit, limit_ref = process_param(limit, 10.0)
        self.config_step, step_ref = process_param(step, 1.0)
        
        # Inputs: [EN, START, LIMIT, STEP]
        self.add_inputs([en, start_ref, limit_ref, step_ref])
        
        # Outputs
        # q_0: ENO (Boolean)
        self._add_output(mem_types_t.MEM_B, data=False)
        # q_1: ITER (Float) - current iterator value
        self._add_output(mem_types_t.MEM_F, data=0.0)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack FOR block configuration data.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][data...]
        - packet_id 0x00: Float constants [start, limit, step]
        - packet_id 0x10: Config [chain_len:u16, condition:u8, operator:u8]
        """
        packets = []
        
        # Packet 1: Float constants (packet_id = 0x00)
        header1 = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CONSTANTS
        )
        payload1 = struct.pack('<fff', self.config_start, self.config_limit, self.config_step)
        packets.append(header1 + payload1)
        
        # Packet 2: Loop config (packet_id = 0x01)
        header2 = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        payload2 = struct.pack('<HBB', self.chain_len, int(self.condition), int(self.operator))
        packets.append(header2 + payload2)
        
        return packets
    
    def __repr__(self) -> str:
        return (f"BlockFor(idx={self.idx}, chain={self.chain_len}, "
                f"start={self.config_start}, limit={self.config_limit}, step={self.config_step}, "
                f"cond={self.condition.name}, op={self.operator.name})")
