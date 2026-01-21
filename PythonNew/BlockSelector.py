import struct
from typing import List, Union
from BlockBase import BlockBase
from EmulatorMemoryReferences import Ref
from BlocksStorage import BlocksStorage
from Enums import block_type_t, emu_types_t, emu_block_header_t


"""
Block Selector - Dynamically switches output to one of multiple provided references

At parse time:
- Creates a dummy bool output (hardcoded)
- Stores multiple output option references

At runtime:
- The C code overwrites the output instance pointer based on selector input
- Points to the selected reference from the options list

Example Usage:
    from EmulatorMemory import EmulatorMemory
    from EmulatorMemoryReferences import Ref
    from BlocksStorage import BlocksStorage
    from BlockSelector import BlockSelector
    from Enums import emu_types_t as DataTypes
    
    # Setup
    mem_glob = EmulatorMemory(context_id=0)
    mem_blk = EmulatorMemory(context_id=1)
    Ref.register_memory(mem_glob)
    Ref.register_memory(mem_blk)
    storage = BlocksStorage(mem_blk)
    
    # Create variables
    mem_glob.add("x", DataTypes.DATA_F, 0.5)
    mem_glob.add("y", DataTypes.DATA_UI8, 100)
    mem_glob.add("z", DataTypes.DATA_I16, -42)
    mem_glob.add("setting", DataTypes.DATA_UI8, 0)  # Selector value
    mem_glob.recalculate_indices()
    
    # Create selector block
    selector = BlockSelector(
        block_idx=0,
        storage=storage,
        selector=Ref("setting"),
        options=[Ref("x"), Ref("y"), Ref("z")]
    )
    
    # Use selector output in other blocks
    # math_block = BlockMath(1, storage, "selector_out * 2", [selector.out[0]])
    # At runtime: when setting=0 -> uses x, setting=1 -> uses y, setting=2 -> uses z
"""


class BlockSelector(BlockBase):
    """
    Block Selector - Multiplexer that dynamically switches between multiple variable references.
    
    The selector block acts as a runtime multiplexer, allowing one input signal to select
    which of several variable references should be used as the output. Unlike other blocks,
    the selector doesn't compute - it just redirects pointer access.
    
    Architecture:
    - Parse time: Creates dummy bool output, stores all option references
    - Runtime: C code overwrites output instance pointer based on selector value
    
    Args:
        block_idx (int): Unique block identifier
        storage (BlocksStorage): Storage manager for block registration
        selector (Ref): Input reference that determines which option is selected (typically UINT8)
        options (List[Ref]): List of 1-16 variable references to choose from
    
    Raises:
        ValueError: If selector is not a Ref
        ValueError: If options is not a list or is empty
        ValueError: If more than 16 options provided
        ValueError: If any option is not a Ref
    
    Note:
        - The selector input value should be in range [0, len(options)-1]
        - Out-of-bounds selector values will trigger runtime error in C code
        - All options can have different types (FLOAT, INT, BOOL, arrays, etc.)
        - Downstream blocks should be compatible with ALL option types
    """
    
    def __init__(self, 
                 block_idx: int, 
                 storage: BlocksStorage,
                 selector: Ref,
                 options: List[Ref]):
        
        if not isinstance(selector, Ref):
            raise ValueError(f"BlockSelector ID {block_idx}: Selector input must be a Reference (Ref), got {type(selector)}")
        
        if not isinstance(options, list) or len(options) == 0:
            raise ValueError(f"BlockSelector ID {block_idx}: Options must be a non-empty list of Ref")
        
        if len(options) > 16:
            raise ValueError(f"BlockSelector ID {block_idx}: Maximum 16 options supported, got {len(options)}")
        
        for i, opt in enumerate(options):
            if not isinstance(opt, Ref):
                raise ValueError(f"BlockSelector ID {block_idx}: Option {i} must be a Reference (Ref), got {type(opt)}")
        
        self.options = options
        self.option_count = len(options)
        
        # Initialize with dummy bool output (will be overwritten at runtime by C code)
        super().__init__(
            block_idx=block_idx,
            block_type=block_type_t.BLOCK_SELECTOR,
            storage=storage,
            inputs=[selector],
            output_defs=[(emu_types_t.DATA_B, [1,1,1,1])]  # Hardcoded bool table output to fit any type de
        )

    def get_output_options_packets(self) -> List[bytes]:
        """
        Generate packets for all selectable output options.
        These are sent as separate packets with subtypes 0xB0-0xBF
        """
        packets = []
        for i, option in enumerate(self.options):
            if i >= 16:
                print(f"[WARNING] BlockSelector {self.block_idx}: Skipping option {i} (max 16)")
                break
            
            try:
                # Build the reference node for this option
                ref_node = option.build()
                node_bytes = ref_node.to_bytes()
                
                # Subtype: 0xB0 + option_index (0xB0, 0xB1, 0xB2, ...)
                subtype = 0xB0 + i
                header = self._pack_common_header(subtype)
                packets.append(header + node_bytes)
                
            except Exception as e:
                print(f"[ERROR] BlockSelector {self.block_idx} Option {i}: {e}")
                raise
        
        return packets

    def get_custom_data_packet(self) -> List[bytes]:
        """
        No custom data packet needed for selector block.
        Options are sent via get_output_options_packets()
        """
        return []

    def get_all_packets(self) -> List[bytes]:
        """
        Override to include output options packets
        """
        packets = []
        
        # 1. Definition packet (config)
        packets.append(self.get_definition_packet())
        
        # 2. Input packets (selector signal)
        packets.extend(self.get_input_refs_packets())
        packets.extend(self.get_output_refs_packets())
        # 3. Output options packets (the selectable references)
        packets.extend(self.get_output_options_packets())
        
        # 4. Custom data packets (none for selector)
        packets.extend(self.get_custom_data_packet())
        
        return packets

    def get_hex_with_comments(self) -> str:

        lines = []

        
        all_packets = self.get_all_packets()
        for i, packet in enumerate(all_packets):
            hex_str = packet.hex().upper()
            formatted = ' '.join(hex_str[j:j+2] for j in range(0, len(hex_str), 2))
            
            if i == 0:
                comment = "# Definition#"
            elif i == 1:
                comment = "# Input: Selector#"
            elif i >= 2 and i < 2 + self.option_count:
                opt_idx = i - 2
                comment = f"# Output Option# {opt_idx}#"
            else:
                comment = ""
            
            lines.append(f"{formatted}  {comment}")
        
        lines.append("")
        return '\n'.join(lines)

    def __str__(self):
        return self.get_hex_with_comments()
