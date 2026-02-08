import struct
from typing import Optional, Union, List
from enum import IntEnum

from Block import Block
from Enums import block_types_t, mem_types_t, packet_header_t, block_packet_id_t
from MemAcces import Ref
from Mem import mem_context_t


class CounterMode(IntEnum):
    """Counter operation modes."""
    ON_RISING   = 0  # Count on rising edge (standard counter)
    WHEN_ACTIVE = 1  # Count continuously when active (accumulator)


class BlockCounter(Block):
    """
    Up/Down Counter block.
    
    Inputs:
        - in_0 (CU): Count Up trigger (Boolean)
        - in_1 (CD): Count Down trigger (Boolean)
        - in_2 (R): Reset trigger (Boolean)
        - in_3 (STEP): Step value (optional, overrides config)
        - in_4 (MAX): Maximum limit (optional, overrides config)
        - in_5 (MIN): Minimum limit (optional, overrides config)
    
    Outputs:
        - q_0 (ENO): Enable output (Boolean)
        - q_1 (VAL): Current counter value (Float)
    
    Config:
        - mode: ON_RISING or WHEN_ACTIVE
        - start_val: Initial value
        - step: Count step (default 1.0)
        - limit_max: Maximum value (default 100.0)
        - limit_min: Minimum value (default 0.0)
    
    Usage:
        block = BlockCounter(
            idx=1,
            ctx=my_context,
            cu=Ref("count_up_btn"),
            cd=Ref("count_down_btn"),
            reset=Ref("reset_btn"),
            step=1.0,
            limit_max=100.0,
            limit_min=0.0
        )
    """
    
    def __init__(self,
                 idx: int,
                 ctx: mem_context_t,
                 cu: Optional[Ref] = None,
                 cd: Optional[Ref] = None,
                 reset: Optional[Ref] = None,
                 step: Union[float, int, Ref] = 1.0,
                 limit_max: Union[float, int, Ref] = 100.0,
                 limit_min: Union[float, int, Ref] = 0.0,
                 start_val: float = 0.0,
                 mode: CounterMode = CounterMode.ON_RISING):
        """
        Create a Counter block.
        
        :param idx: Block index
        :param ctx: Context for output variables
        :param cu: Count Up input Ref
        :param cd: Count Down input Ref
        :param reset: Reset input Ref
        :param step: Step value (constant or Ref)
        :param limit_max: Maximum limit (constant or Ref)
        :param limit_min: Minimum limit (constant or Ref)
        :param start_val: Initial counter value
        :param mode: Counter mode (ON_RISING or WHEN_ACTIVE)
        """
        # Initialize base Block
        super().__init__(idx=idx, block_type=block_types_t.BLOCK_COUNTER, ctx=ctx)
        
        self.mode = mode
        self.config_start = float(start_val)
        self.config_step = 1.0
        self.config_max = 100.0
        self.config_min = 0.0
        
        # Process parameters (can be constant or Ref)
        def process_param(arg, default_val):
            if isinstance(arg, (int, float)):
                return float(arg), None
            elif isinstance(arg, Ref):
                return default_val, arg
            return default_val, None
        
        self.config_step, step_ref = process_param(step, 1.0)
        self.config_max, max_ref = process_param(limit_max, 100.0)
        self.config_min, min_ref = process_param(limit_min, 0.0)
        
        # Inputs: [CU, CD, R, STEP, MAX, MIN]
        self.add_inputs([cu, cd, reset, step_ref, max_ref, min_ref])
        
        # Outputs
        # q_0: ENO (Boolean)
        self._add_output(mem_types_t.MEM_B, data=True)
        # q_1: VAL (Float) - counter value
        self._add_output(mem_types_t.MEM_F, data=start_val)
    
    def pack_data(self) -> List[bytes]:
        """
        Pack counter configuration data.
        
        Format: [BA][block_idx:u16][block_type:u8][packet_id:u8][mode:u8][start:f][step:f][max:f][min:f]
        """
        packets = []
        
        # Config packet (packet_id = 0x01: counter config)
        header = struct.pack('<BHBB',
            packet_header_t.PACKET_H_BLOCK_DATA,
            self.idx,
            self.block_type,
            block_packet_id_t.PKT_CFG
        )
        # Pack as floats (4 bytes each)
        payload = struct.pack('<Bffff',
            int(self.mode),
            self.config_start,
            self.config_step,
            self.config_max,
            self.config_min
        )
        packets.append(header + payload)
        
        return packets
