"""
Human-readable display utilities for Mem contexts and instances
"""
from typing import Optional
from Mem import Mem, mem_types_t


def print_contexts_table(mem: Mem, title: Optional[str] = None):
    """
    Print all contexts and their instances in a human-readable table format.
    Shows: Ctx, Type, Alias, Index, Dims, Data (full)
    Each context gets its own separate table.
    
    Args:
        mem: Mem instance to display
        title: Optional title for the table
    """
    if title:
        print(f"\n{'='*100}")
        print(f"{title:^100}")
        print(f"{'='*100}\n")
    else:
        print(f"\n{'='*100}")
        print(f"{'MEMORY INSTANCES':^100}")
        print(f"{'='*100}\n")
    
    total_instances = 0
    has_any_instances = False
    
    # Print separate table for each context
    for ctx in mem.contexts:
        # Collect and sort all instances by type then by index
        all_instances = []
        for mem_type in sorted(ctx.storage.keys()):
            for inst in ctx.storage[mem_type]:
                all_instances.append((mem_type, inst))
        
        if not all_instances:
            continue
        
        has_any_instances = True
        total_instances += len(all_instances)
        
        # Context header
        print(f"Context {ctx.ctx_id} ({len(all_instances)} instances)")
        print("-" * 100)
        print(f"{'Type':<10} {'Alias':<25} {'Index':<7} {'Dims':<15} {'Data'}")
        print("-" * 100)
        
        # Print instances
        for mem_type, inst in all_instances:
            # Format type name
            type_name = mem_type.name if hasattr(mem_type, 'name') else str(mem_type.value)
            
            # Format dimensions
            if inst.head.dims_cnt > 0:
                dims_list = [str(d.value if hasattr(d, 'value') else int(d)) for d in inst.dims]
                dims_str = '[' + ']['.join(dims_list) + ']'
            else:
                dims_str = "scalar"
            
            # Format data (full, not truncated)
            if inst.data is None:
                data_str = "(no data)"
            else:
                data_str = str(inst.data)
            
            print(f"{type_name:<10} {inst.alias:<25} {inst.my_index:<7} {dims_str:<15} {data_str}")
        
        print()  # Blank line between contexts
    
    if not has_any_instances:
        print("  (No instances defined)\n")
    
    print(f"{'='*100}\n")


def print_context_summary(mem: Mem):
    """
    Print a compact summary of all contexts showing type distribution.
    
    Args:
        mem: Mem instance to display
    """
    print(f"\n{'='*80}")
    print(f"{'CONTEXT SUMMARY':^80}")
    print(f"{'='*80}")
    print(f"{'Ctx':<5} {'Total':<7} {'MEM_U8':<8} {'MEM_U16':<9} {'MEM_U32':<9} {'MEM_I16':<9} {'MEM_I32':<9} {'MEM_B':<7} {'MEM_F':<7}")
    print("-" * 80)
    
    type_order = [
        mem_types_t.MEM_U8,
        mem_types_t.MEM_U16,
        mem_types_t.MEM_U32,
        mem_types_t.MEM_I16,
        mem_types_t.MEM_I32,
        mem_types_t.MEM_B,
        mem_types_t.MEM_F,
    ]
    
    total_overall = 0
    for ctx in mem.contexts:
        total = sum(len(instances) for instances in ctx.storage.values())
        if total == 0:
            continue
        
        total_overall += total
        counts = [len(ctx.storage[t]) for t in type_order]
        
        print(f"{ctx.ctx_id:<5} {total:<7} {counts[0]:<8} {counts[1]:<9} {counts[2]:<9} {counts[3]:<9} {counts[4]:<9} {counts[5]:<7} {counts[6]:<7}")
    
    print("-" * 80)
    print(f"{'TOTAL':<5} {total_overall:<7}")
    print(f"{'='*80}\n")


def print_packet_statistics(mem: Mem):
    """
    Print statistics about generated packets.
    
    Args:
        mem: Mem instance to display
    """
    print(f"\n{'='*80}")
    print(f"{'PACKET STATISTICS':^80}")
    print(f"{'='*80}")
    
    cfg_packets = mem.generate_cfg_packets()
    inst_packets = mem.generate_instance_packets()
    scalar_packets = mem.generate_scalar_data_packets()
    array_packets = mem.generate_array_data_packets()
    
    total_cfg = sum(len(p) for p in cfg_packets)
    total_inst = sum(len(p) for p in inst_packets)
    total_scalar = sum(len(p) for p in scalar_packets)
    total_array = sum(len(p) for p in array_packets)
    
    print(f"{'Packet Type':<30} {'Count':<10} {'Total Bytes':<15} {'Avg Size':<10}")
    print("-" * 80)
    print(f"{'Context Config (0xF0)':<30} {len(cfg_packets):<10} {total_cfg:<15} {total_cfg/max(len(cfg_packets),1):<10.1f}")
    print(f"{'Instance (0xF1)':<30} {len(inst_packets):<10} {total_inst:<15} {total_inst/max(len(inst_packets),1):<10.1f}")
    print(f"{'Scalar Data (0xFA)':<30} {len(scalar_packets):<10} {total_scalar:<15} {total_scalar/max(len(scalar_packets),1) if scalar_packets else 0:<10.1f}")
    print(f"{'Array Data (0xFB)':<30} {len(array_packets):<10} {total_array:<15} {total_array/max(len(array_packets),1) if array_packets else 0:<10.1f}")
    print("-" * 80)
    
    total_packets = len(cfg_packets) + len(inst_packets) + len(scalar_packets) + len(array_packets)
    total_bytes = total_cfg + total_inst + total_scalar + total_array
    
    print(f"{'TOTAL':<30} {total_packets:<10} {total_bytes:<15} {total_bytes/max(total_packets,1):<10.1f}")
    print(f"{'='*80}\n")

if __name__ == '__main__':
    """Simple display test"""
    
    # Create memory instance
    mem = Mem(pkt_size=256)
    
    # Add some user variables (context 0)
    mem.add_instance(0, mem_types_t.MEM_I32, 'count', data=42)
    mem.add_instance(0, mem_types_t.MEM_F, 'speed', data=25.5)
    mem.add_instance(0, mem_types_t.MEM_B, 'active', data=True)
    mem.add_instance(0, mem_types_t.MEM_F, 'sensors', data=[12.5, 13.1, 12.9], dims=[3])
    mem.add_instance(0, mem_types_t.MEM_I32, 'matrix', data=[1, 2, 3, 4, 5, 6], dims=[2, 3])
    
    # Add some block outputs (context 1)
    mem.add_instance(1, mem_types_t.MEM_F, 'blk_0_q_out', data=15.7)
    mem.add_instance(1, mem_types_t.MEM_B, 'blk_0_q_done', data=False)
    mem.add_instance(1, mem_types_t.MEM_I32, 'blk_0_q_count', data=100)
 
    # Display
    print("=" * 100)
    print("SIMPLE MEMORY DISPLAY TEST")
    print("=" * 100)
    
    print_context_summary(mem)
    print_contexts_table(mem)