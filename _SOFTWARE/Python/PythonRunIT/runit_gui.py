"""
runIT BLE Control Panel & VM Program Builder.

Features:
- VM Program & Objects Builder:
  * High-level relational VMObject creation with automatic ID assignment.
  * Optional naming (named user variables vs anonymous intermediate buffers).
  * Chained & relational accessors (auto-deduplicated, topologically ordered).
  * Presets (Guide pipeline 5-obj test, Sensor Array test).
  * 1-Click Compile to wire packets (Class 0x04 VM Loader).
  * 1-Click BLE Upload & Live Execution Runner.
  * Quick runtime control (Reset 0x40, Stop 0x48, Start 0x48).
- BLE & Decoder Explorer:
  * GATT Characteristic discovery & tree.
  * Auto-generated packet builder from decoder_types.py.
  * Raw hex send & single characteristic read.
- Batch Sender:
  * Loads .txt files and streams hex packets line-by-line.
- Live Monitor / Log:
  * Displays TX commands and live RX telemetry notifications.
"""
from __future__ import annotations
import asyncio
import ctypes as ct
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import ttk, scrolledtext, filedialog
from typing import Optional, List, Dict, Any

import decoder_types as dt
from ble_client import RunITBLEClient, format_hex, format_text
from vm import (
    VMProgram,
    VMObject,
    Accessor,
    AccessorGenerator,
    VMType,
    VMFlags,
    VMIndexKind,
    VMExecCommand,
    encode_vm_reset,
    encode_vm_exec,
)

DEVICE_NAME = "runit"


class StdoutRedirector:
    """Mirrors print() output into the GUI's log pane."""

    def __init__(self, log_func):
        self.log_func = log_func
        self.buffer = ""

    def write(self, string):
        self.buffer += string
        if "\n" in self.buffer:
            lines = self.buffer.split("\n")
            for line in lines[:-1]:
                if line:
                    self.log_func(line)
            self.buffer = lines[-1]

    def flush(self):
        pass


class RunITGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("runIT - Device Control Panel & VM Builder")
        self.geometry("1120x860")
        self.minsize(960, 720)

        self.ble = RunITBLEClient(device_name=DEVICE_NAME)
        self.loop = asyncio.new_event_loop()
        self.selected_cid = None
        self.batch_lines: list[str] = []

        # Decoder registry
        self.packets_by_class: dict[int, list[type]] = {}
        for struct_cls in dt.PACKET_REGISTRY:
            self.packets_by_class.setdefault(int(struct_cls._class_header_), []).append(struct_cls)

        self.field_vars: dict[str, tuple[tk.Variable, type]] = {}

        # Active VM Program state
        self.vm_program = VMProgram("gui_program")
        self.vm_compiled = None
        self.gui_obj_records: list[dict] = []

        self._build_ui()
        sys.stdout = StdoutRedirector(self.log)

        # Load default guide preset so user has an immediate ready-to-test setup
        self.load_guide_preset()

        threading.Thread(target=self._start_async_loop, daemon=True).start()

    # ------------------------------------------------------------------ UI Layout

    def _build_ui(self):
        # Top toolbar
        top = ttk.Frame(self)
        top.pack(fill=tk.X, padx=10, pady=8)

        self.btn_connect = ttk.Button(top, text="Connect BLE", command=self.on_connect_click)
        self.btn_connect.pack(side=tk.LEFT)

        self.btn_refresh = ttk.Button(
            top, text="Refresh Characteristics", command=self.on_refresh_click, state=tk.DISABLED
        )
        self.btn_refresh.pack(side=tk.LEFT, padx=6)

        self.device_status_var = tk.StringVar(value="Status: Disconnected")
        ttk.Label(top, textvariable=self.device_status_var, foreground="#666").pack(side=tk.LEFT, padx=12)

        self.target_label_var = tk.StringVar(value="Target: Auto (RX / 0xFFE2)")
        ttk.Label(top, textvariable=self.target_label_var, font=("Segoe UI", 9, "bold")).pack(side=tk.RIGHT)

        # Notebook tabs
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=4)

        tab_vm = ttk.Frame(self.notebook)
        tab_ble = ttk.Frame(self.notebook)
        tab_batch = ttk.Frame(self.notebook)

        self.notebook.add(tab_vm, text="  ⚡ VM Program & Objects  ")
        self.notebook.add(tab_ble, text="  🔍 BLE & Packet Explorer  ")
        self.notebook.add(tab_batch, text="  📁 Batch File Sender  ")

        self._build_vm_tab(tab_vm)
        self._build_ble_tab(tab_ble)
        self._build_batch_tab(tab_batch)

        # Shared Monitor / Log at bottom
        self._build_monitor(self)

    # ------------------------------------------------------------------ VM Tab

    def _build_vm_tab(self, parent):
        paned = ttk.PanedWindow(parent, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        # --- Left Panel: Object & Variable Manager ---
        left_frame = ttk.LabelFrame(paned, text="VM Objects & Variables (Automatic IDs)")
        paned.add(left_frame, weight=3)

        # Objects Treeview (Tree / Folder hierarchy for nested structs)
        cols = ("idx", "type", "count", "initial", "tagged", "sub")
        self.obj_tree = ttk.Treeview(left_frame, columns=cols, show="tree headings", height=9, selectmode="browse")

        self.obj_tree.heading("#0", text="Object / Field Hierarchy", anchor=tk.W)
        self.obj_tree.column("#0", width=170, anchor=tk.W)

        headers = [
            ("idx", "Rel ID", 50),
            ("type", "Type", 60),
            ("count", "Items", 45),
            ("initial", "Initial Value", 90),
            ("tagged", "Tagged", 50),
            ("sub", "Live Telemetry", 90),
        ]
        for col, label, width in headers:
            self.obj_tree.heading(col, text=label)
            self.obj_tree.column(col, width=width, anchor=tk.W)

        self.obj_tree.tag_configure("folder", font=("Segoe UI", 9, "bold"))
        self.obj_tree.tag_configure("child", font=("Segoe UI", 9))
        self.obj_tree.tag_configure("leaf", font=("Segoe UI", 9))

        self.obj_tree.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)

        # Presets Bar
        preset_bar = ttk.Frame(left_frame)
        preset_bar.pack(fill=tk.X, padx=6, pady=(0, 4))
        ttk.Button(preset_bar, text="Guide Pipeline", command=self.load_guide_preset).pack(
            side=tk.LEFT, padx=3
        )
        ttk.Button(preset_bar, text="Sensor Array", command=self.load_sensor_preset).pack(side=tk.LEFT, padx=3)
        ttk.Button(preset_bar, text="Nested Struct", command=self.load_nested_struct_preset).pack(side=tk.LEFT, padx=3)
        ttk.Button(preset_bar, text="Clear All", command=self.clear_vm_program).pack(side=tk.RIGHT, padx=4)

        # Add Object Form
        form = ttk.LabelFrame(left_frame, text="Add Variable / Object")
        form.pack(fill=tk.X, padx=6, pady=4)

        row0 = ttk.Frame(form)
        row0.pack(fill=tk.X, padx=4, pady=2)
        ttk.Label(row0, text="Name:", width=10).pack(side=tk.LEFT)
        self.var_name_entry = ttk.Entry(row0, width=18)
        self.var_name_entry.pack(side=tk.LEFT, padx=4)
        ttk.Label(row0, text="(blank = anonymous scratch)", foreground="#777", font=("Segoe UI", 8)).pack(
            side=tk.LEFT
        )

        row1 = ttk.Frame(form)
        row1.pack(fill=tk.X, padx=4, pady=2)
        ttk.Label(row1, text="Type:", width=10).pack(side=tk.LEFT)
        self.var_type_var = tk.StringVar(value="F (Float32)")
        type_options = [
            "F (Float32)",
            "U32 (UInt32)",
            "I32 (Int32)",
            "U8 (UInt8)",
            "B (Boolean)",
            "STR (Char/Str)",
            "U64 (UInt64)",
            "PTR (Pointer)",
        ]
        self.var_type_cb = ttk.Combobox(row1, textvariable=self.var_type_var, values=type_options, state="readonly", width=16)
        self.var_type_cb.pack(side=tk.LEFT, padx=4)

        ttk.Label(row1, text="Count:").pack(side=tk.LEFT, padx=(8, 2))
        self.var_count_entry = ttk.Entry(row1, width=6)
        self.var_count_entry.insert(0, "1")
        self.var_count_entry.pack(side=tk.LEFT, padx=2)

        row2 = ttk.Frame(form)
        row2.pack(fill=tk.X, padx=4, pady=2)
        ttk.Label(row2, text="Initial:", width=10).pack(side=tk.LEFT)
        self.var_init_entry = ttk.Entry(row2, width=28)
        self.var_init_entry.pack(side=tk.LEFT, padx=4)
        ttk.Label(row2, text="(e.g. 0.0 or 1,2,3)", foreground="#777", font=("Segoe UI", 8)).pack(side=tk.LEFT)

        row3 = ttk.Frame(form)
        row3.pack(fill=tk.X, padx=4, pady=2)
        self.var_mutable_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(row3, text="Mutable", variable=self.var_mutable_var).pack(side=tk.LEFT, padx=4)

        self.var_sub_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(row3, text="Subscribe Telemetry", variable=self.var_sub_var).pack(side=tk.LEFT, padx=12)

        btn_row = ttk.Frame(form)
        btn_row.pack(fill=tk.X, padx=4, pady=4)
        ttk.Button(btn_row, text="+ Add Named Variable", command=self.on_add_named_var_click).pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(btn_row, text="+ Add Anonymous Buffer", command=self.on_add_anonymous_click).pack(
            side=tk.LEFT, padx=4
        )
        ttk.Button(btn_row, text="- Remove Selected", command=self.on_remove_selected_obj).pack(
            side=tk.RIGHT, padx=4
        )

        # --- Right Panel: Compiled Packets & BLE Runner ---
        right_frame = ttk.LabelFrame(paned, text="Compiled Wire Packets & Device Runner")
        paned.add(right_frame, weight=4)

        # Actions toolbar
        act_bar = ttk.Frame(right_frame)
        act_bar.pack(fill=tk.X, padx=6, pady=4)

        self.btn_compile_vm = ttk.Button(act_bar, text="⚡ Compile Program", command=self.on_compile_vm_click)
        self.btn_compile_vm.pack(side=tk.LEFT, padx=4)

        self.btn_upload_vm = ttk.Button(
            act_bar, text="🚀 Upload & Run (BLE)", command=self.on_upload_vm_click
        )
        self.btn_upload_vm.pack(side=tk.LEFT, padx=4)

        # Quick Control Buttons
        quick_bar = ttk.Frame(right_frame)
        quick_bar.pack(fill=tk.X, padx=6, pady=(0, 4))
        ttk.Label(quick_bar, text="Direct Control:", font=("Segoe UI", 8, "bold")).pack(side=tk.LEFT)
        ttk.Button(quick_bar, text="VM Reset (0x40)", command=self.on_quick_reset_click).pack(
            side=tk.LEFT, padx=3
        )
        ttk.Button(quick_bar, text="VM Stop (0x48)", command=self.on_quick_stop_click).pack(
            side=tk.LEFT, padx=3
        )
        ttk.Button(quick_bar, text="VM Start (0x48)", command=self.on_quick_start_click).pack(
            side=tk.LEFT, padx=3
        )

        # Packets Treeview
        pcols = ("no", "name", "size", "hex")
        self.pkt_tree = ttk.Treeview(right_frame, columns=pcols, show="headings", height=8, selectmode="browse")
        pheaders = [
            ("no", "#", 28),
            ("name", "Packet Name", 190),
            ("size", "Bytes", 45),
            ("hex", "Wire Hex Stream", 280),
        ]
        for col, label, width in pheaders:
            self.pkt_tree.heading(col, text=label)
            self.pkt_tree.column(col, width=width, anchor=tk.W)

        self.pkt_tree.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)
        self.pkt_tree.bind("<<TreeviewSelect>>", self.on_packet_tree_select)

        # Packet Breakdown & Hex Text Box
        self.vm_preview_text = scrolledtext.ScrolledText(
            right_frame, height=9, font=("Consolas", 9), background="#1e1e1e", foreground="#d4d4d4"
        )
        self.vm_preview_text.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)

    # ------------------------------------------------------------------ BLE Tab

    def _build_ble_tab(self, parent):
        body = ttk.Frame(parent)
        body.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        self._build_char_list(body)
        self._build_packet_builder(body)

    def _build_char_list(self, parent):
        frame = ttk.LabelFrame(parent, text="Discovered Characteristics")
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 4))

        cols = ("id", "uuid", "name", "props", "monitor")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=14, selectmode="browse")
        for col, label, width in [
            ("id", "ID", 30),
            ("uuid", "UUID", 70),
            ("name", "Name", 130),
            ("props", "Properties", 130),
            ("monitor", "Monitor", 65),
        ]:
            self.tree.heading(col, text=label)
            self.tree.column(col, width=width, anchor=tk.W)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.tree.bind("<<TreeviewSelect>>", self.on_tree_select)

        btn_row = ttk.Frame(frame)
        btn_row.pack(fill=tk.X, padx=5, pady=(0, 5))
        self.btn_toggle_monitor = ttk.Button(
            btn_row, text="Toggle Monitor", command=self.on_toggle_monitor_click, state=tk.DISABLED
        )
        self.btn_toggle_monitor.pack(side=tk.LEFT)

        self.btn_read = ttk.Button(
            btn_row, text="Read Selected", command=self.on_read_click, state=tk.DISABLED
        )
        self.btn_read.pack(side=tk.LEFT, padx=6)

    def _build_packet_builder(self, parent):
        frame = ttk.LabelFrame(parent, text="Packet Builder (decoder_types.py)")
        frame.grid(row=0, column=1, sticky="nsew", padx=(4, 0))

        ttk.Label(frame, text="Class:").grid(row=0, column=0, padx=8, pady=5, sticky=tk.E)
        self.class_var = tk.StringVar()
        self.class_cb = ttk.Combobox(frame, textvariable=self.class_var, state="readonly", width=28)
        self.class_cb["values"] = [f"{c.name} (0x{int(c.value):02X})" for c in dt.DecoderClass]
        self.class_cb.grid(row=0, column=1, padx=8, pady=5, sticky=tk.W)
        self.class_cb.bind("<<ComboboxSelected>>", self.on_class_select)

        ttk.Label(frame, text="Packet:").grid(row=1, column=0, padx=8, pady=5, sticky=tk.E)
        self.packet_var = tk.StringVar()
        self.packet_cb = ttk.Combobox(frame, textvariable=self.packet_var, state="readonly", width=28)
        self.packet_cb.grid(row=1, column=1, padx=8, pady=5, sticky=tk.W)
        self.packet_cb.bind("<<ComboboxSelected>>", self.on_packet_select)

        self.form_frame = ttk.Frame(frame)
        self.form_frame.grid(row=2, column=0, columnspan=2, sticky="ew", padx=8, pady=5)

        self.preview_var = tk.StringVar(value="Preview: -")
        ttk.Label(frame, textvariable=self.preview_var, font=("Consolas", 9), foreground="#0057b8").grid(
            row=3, column=0, columnspan=2, padx=8, pady=(5, 0), sticky=tk.W
        )

        self.btn_send_packet = ttk.Button(
            frame, text="Send Packet", command=self.on_send_packet_click, state=tk.DISABLED
        )
        self.btn_send_packet.grid(row=4, column=0, columnspan=2, padx=8, pady=8, sticky=tk.EW)

        ttk.Separator(frame, orient=tk.HORIZONTAL).grid(
            row=5, column=0, columnspan=2, sticky="ew", padx=8, pady=5
        )

        ttk.Label(frame, text="Raw hex:").grid(row=6, column=0, padx=8, pady=5, sticky=tk.E)
        self.raw_hex_var = tk.StringVar()
        ttk.Entry(frame, textvariable=self.raw_hex_var, width=30).grid(
            row=6, column=1, padx=8, pady=5, sticky=tk.W
        )
        self.btn_send_raw = ttk.Button(
            frame, text="Send Raw Hex", command=self.on_send_raw_click, state=tk.DISABLED
        )
        self.btn_send_raw.grid(row=7, column=0, columnspan=2, padx=8, pady=(0, 8), sticky=tk.EW)

        if self.class_cb["values"]:
            self.class_cb.current(0)
            self.on_class_select(None)

    # ------------------------------------------------------------------ Batch Tab

    def _build_batch_tab(self, parent):
        frame = ttk.LabelFrame(parent, text="Batch Sender (.txt file, one hex packet per line)")
        frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        top_row = ttk.Frame(frame)
        top_row.pack(fill=tk.X, padx=8, pady=8)

        self.btn_load_file = ttk.Button(top_row, text="Load .txt File...", command=self.on_load_file_click)
        self.btn_load_file.pack(side=tk.LEFT)

        self.batch_status_var = tk.StringVar(value="No file loaded.")
        ttk.Label(top_row, textvariable=self.batch_status_var).pack(side=tk.LEFT, padx=12)

        self.btn_send_batch = ttk.Button(
            top_row, text="Send All Lines", command=self.on_send_batch_click, state=tk.DISABLED
        )
        self.btn_send_batch.pack(side=tk.RIGHT)

        self.batch_preview_text = scrolledtext.ScrolledText(
            frame, height=14, font=("Consolas", 9), background="#1e1e1e", foreground="#d4d4d4"
        )
        self.batch_preview_text.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

    # ------------------------------------------------------------------ Monitor

    def _build_monitor(self, parent):
        frame = ttk.LabelFrame(parent, text="Live Monitor & Event Log")
        frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 8))

        self.log_text = scrolledtext.ScrolledText(
            frame, height=12, font=("Consolas", 9), background="#121212", foreground="#eaeaea"
        )
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.log_text.tag_configure("ok", foreground="#3ddc63")
        self.log_text.tag_configure("error", foreground="#ff5c5c")
        self.log_text.tag_configure("info", foreground="#61afef")
        self.log_text.tag_configure("tx", foreground="#e5c07b")
        self.log_text.tag_configure("rx", foreground="#98c379")

    # -------------------------------------------------------------- Logging

    def log(self, msg: str, tag: Optional[str] = None):
        self.after(0, self._append_log, msg, tag)

    def _append_log(self, msg: str, tag: Optional[str]):
        text = str(msg)
        lowered = text.lower()
        if not tag:
            if any(k in lowered for k in ("error", "fail", "cannot")):
                tag = "error"
            elif "[tx" in lowered or "->" in lowered:
                tag = "tx"
            elif "[rx" in lowered or "<-" in lowered:
                tag = "rx"
            else:
                tag = "ok"
        self.log_text.insert(tk.END, text + "\n", tag)
        self.log_text.see(tk.END)

    # -------------------------------------------------------------- VM Program Logic

    def parse_type_enum(self, type_str: str) -> VMType:
        if "U32" in type_str:
            return VMType.U32
        elif "I32" in type_str:
            return VMType.I32
        elif "U8" in type_str:
            return VMType.U8
        elif "B " in type_str or "(Bool" in type_str:
            return VMType.B
        elif "STR" in type_str:
            return VMType.STR
        elif "U64" in type_str:
            return VMType.U64
        elif "PTR" in type_str:
            return VMType.PTR
        return VMType.F

    def parse_initial_value(self, val_str: str, vtype: VMType, count: int) -> Optional[Any]:
        val_str = val_str.strip()
        if not val_str:
            return None
        parts = [p.strip() for p in val_str.split(",") if p.strip()]
        if not parts:
            return None

        def cast_one(s: str):
            if vtype == VMType.F:
                return float(s)
            elif vtype == VMType.B:
                return s.lower() in ("1", "true", "yes")
            else:
                return int(s, 0)

        if count == 1 and len(parts) == 1:
            return cast_one(parts[0])
        return [cast_one(p) for p in parts]

    def on_add_named_var_click(self):
        name = self.var_name_entry.get().strip()
        if not name:
            self.log("Note: Named variable requires a name. For anonymous buffers, click 'Add Anonymous Buffer'.")
            return
        self._add_object_to_prog(name)

    def on_add_anonymous_click(self):
        self._add_object_to_prog(None)

    def _add_object_to_prog(self, name: Optional[str]):
        try:
            vtype = self.parse_type_enum(self.var_type_var.get())
            count = max(1, int(self.var_count_entry.get().strip() or "1"))
            init_val = self.parse_initial_value(self.var_init_entry.get(), vtype, count)
            mutable = self.var_mutable_var.get()
            subscribe = self.var_sub_var.get()

            if name:
                obj = self.vm_program.add_var(name=name, type=vtype, count=count, initial=init_val, mutable=mutable)
            else:
                obj = self.vm_program.add_temp(type=vtype, count=count, initial=init_val, mutable=mutable)

            if subscribe:
                self.vm_program.subscribe(obj)

            # Auto create default accessor for object index 0
            self.vm_program.add_accessor(obj[0])

            self.refresh_obj_table()
            self.log(f"Added VM object: {obj}")

            # Clear inputs
            self.var_name_entry.delete(0, tk.END)
            self.var_init_entry.delete(0, tk.END)
        except Exception as e:
            self.log(f"Error creating object: {e}")

    def on_remove_selected_obj(self):
        sel = self.obj_tree.selection()
        if not sel:
            return
        idx = int(self.obj_tree.item(sel[0], "values")[0])
        if 0 <= idx < len(self.vm_program.objects):
            obj = self.vm_program.objects.pop(idx)
            # Clear accessors and re-register remaining
            new_acc_gen = AccessorGenerator()
            for o in self.vm_program.objects:
                new_acc_gen.register(o[0])
            self.vm_program.accessor_gen = new_acc_gen
            if obj in self.vm_program.subscribed_objects:
                self.vm_program.subscribed_objects.remove(obj)
            self.refresh_obj_table()
            self.log(f"Removed object: {obj}")

    def clear_vm_program(self):
        self.vm_program = VMProgram("gui_program")
        self.vm_compiled = None
        self.refresh_obj_table()
        self.pkt_tree.delete(*self.pkt_tree.get_children())
        self.vm_preview_text.delete("1.0", tk.END)
        self.log("Cleared VM Program.")

    def load_guide_preset(self):
        """Loads the exact 5-object 3-accessor pipeline from runit_guide_test.c."""
        self.clear_vm_program()
        self.vm_program.name = "guide_pipeline"

        o0 = self.vm_program.add_temp(type=VMType.F, count=1, initial=0.0)  # Sum
        o1 = self.vm_program.add_temp(type=VMType.F, count=1)               # i
        o2 = self.vm_program.add_temp(type=VMType.F, count=1)               # Math
        o3 = self.vm_program.add_temp(type=VMType.B, count=1)               # eno1
        o4 = self.vm_program.add_temp(type=VMType.B, count=1)               # eno2

        self.vm_program.add_accessor(o0[0])
        self.vm_program.add_accessor(o1[0])
        self.vm_program.add_accessor(o2[0])

        self.vm_program.subscribe(o0, o1)
        self.refresh_obj_table()
        self.on_compile_vm_click()
        self.log("Loaded preset: Guide Pipeline (5 Objects, 3 Accessors, Initial Float 0.0)")

    def load_sensor_preset(self):
        """Loads named user variables: temperature array and sensor_id."""
        self.clear_vm_program()
        self.vm_program.name = "sensor_stream"

        temp = self.vm_program.add_var("temperature", type=VMType.F, count=4, initial=[21.5, 22.0, 22.8, 23.1])
        sensor_id = self.vm_program.add_var("sensor_id", type=VMType.U32, count=1, initial=42)
        scratch = self.vm_program.add_temp(type=VMType.F, count=1)

        idx_acc = self.vm_program.add_accessor(sensor_id[0])
        self.vm_program.add_accessor(temp[idx_acc])  # Relational indirect accessor!
        self.vm_program.add_accessor(scratch[0])

        self.vm_program.subscribe(temp, sensor_id)
        self.refresh_obj_table()
        self.on_compile_vm_click()
        self.log("Loaded preset: Sensor Stream (Named tagged variables + Relational REF Accessor)")

    def load_nested_struct_preset(self):
        """Loads a nested struct: Motor container linking speed, current, enabled."""
        self.clear_vm_program()
        self.vm_program.name = "nested_motor_struct"

        motor = self.vm_program.add_struct(
            "Motor",
            {
                "speed": (VMType.F, 120.5),
                "current": (VMType.F, 2.3),
                "enabled": (VMType.B, True),
            },
        )
        self.vm_program.add_accessor(motor.speed[0])
        self.vm_program.add_accessor(motor["current"][0])
        self.vm_program.add_accessor(motor.field("enabled"))

        self.vm_program.subscribe(motor, motor.children[0])
        self.refresh_obj_table()
        self.on_compile_vm_click()
        self.log("Loaded preset: Nested Motor Struct (VM_OBJ_PTR container with speed, current, enabled)")

    def refresh_obj_table(self):
        self.obj_tree.delete(*self.obj_tree.get_children())

        # Collect set of all objects that are children of some container
        children_set = set()
        for obj in self.vm_program.objects:
            if obj.type == VMType.PTR and obj.children:
                for c in obj.children:
                    children_set.add(c)

        rendered = set()

        def _render_node(parent_id: str, obj: VMObject):
            rendered.add(obj)
            i = self.vm_program.objects.index(obj) if obj in self.vm_program.objects else "?"
            tagged_disp = "YES" if obj.is_tagged() else "no"
            sub_disp = "SUBSCRIBED" if obj in self.vm_program.subscribed_objects else "-"
            init_disp = str(obj.initial_value) if obj.initial_value is not None else "-"

            is_container = (obj.type == VMType.PTR and bool(obj.children))
            if is_container:
                label = f"📁 {obj.name or f'<container_{i}>'}"
                child_count = len(obj.children)
                node_id = self.obj_tree.insert(
                    parent_id,
                    tk.END,
                    text=label,
                    values=(i, obj.type.name, obj.count, f"[{child_count} fields]", tagged_disp, sub_disp),
                    open=True,
                    tags=("folder",),
                )
                for child in obj.children:
                    _render_node(node_id, child)
            else:
                tag = "child" if parent_id else "leaf"
                label = f"  📄 {obj.name or f'<var_{i}>'}" if parent_id else f"📄 {obj.name or f'<var_{i}>'}"
                self.obj_tree.insert(
                    parent_id,
                    tk.END,
                    text=label,
                    values=(i, obj.type.name, obj.count, init_disp, tagged_disp, sub_disp),
                    tags=(tag,),
                )

        # 1. Render all top-level objects (containers & standalone variables)
        for obj in self.vm_program.objects:
            if obj not in children_set:
                _render_node("", obj)

        # 2. Render any orphan children that weren't reached (safety fallback)
        for obj in self.vm_program.objects:
            if obj not in rendered:
                _render_node("", obj)

    def on_compile_vm_click(self):
        try:
            self.vm_compiled = self.vm_program.compile(arena_size=2048)
            self.pkt_tree.delete(*self.pkt_tree.get_children())
            for i, pkt in enumerate(self.vm_compiled.packets, 1):
                hex_str = " ".join(f"{b:02X}" for b in pkt.data)
                self.pkt_tree.insert("", tk.END, values=(i, pkt.name, len(pkt.data), hex_str))

            self.vm_preview_text.delete("1.0", tk.END)
            self.vm_preview_text.insert(tk.END, self.vm_compiled.dump_hex() + "\n")
            self.log(
                f"Compiled VM Program: {len(self.vm_compiled.packets)} packets, "
                f"{len(self.vm_compiled.objects)} objects, {len(self.vm_compiled.accessors)} accessors."
            )
        except Exception as e:
            self.log(f"Compile error: {e}")

    def on_packet_tree_select(self, _event):
        sel = self.pkt_tree.selection()
        if not sel or not self.vm_compiled:
            return
        idx = int(self.pkt_tree.item(sel[0], "values")[0]) - 1
        if 0 <= idx < len(self.vm_compiled.packets):
            pkt = self.vm_compiled.packets[idx]
            hex_str = " ".join(f"{b:02X}" for b in pkt.data)
            self.vm_preview_text.delete("1.0", tk.END)
            self.vm_preview_text.insert(tk.END, f"Packet #{idx+1}: {pkt.name}\n")
            self.vm_preview_text.insert(tk.END, f"Length: {len(pkt.data)} bytes\n")
            self.vm_preview_text.insert(tk.END, f"Hex: {hex_str}\n\n")
            self.vm_preview_text.insert(tk.END, self.vm_compiled.dump_hex())

    # -------------------------------------------------------------- Device Upload & Runner

    def _find_default_rx_char(self):
        """Finds the writable characteristic for feeding wire packets (0xFFE2 / RX)."""
        # First preference: short UUID 0xFFE2 or name containing RX
        for info in self.ble.characteristics.values():
            if "ffe2" in info.short_uuid.lower() or "rx" in info.name.lower():
                return info
        # Second preference: any characteristic with write property
        for info in self.ble.characteristics.values():
            if "write" in info.properties or "write-without-response" in info.properties:
                return info
        return None

    def _find_default_tx_char(self):
        """Finds the notify characteristic for live telemetry (0xFFE1 / TX)."""
        # First preference: short UUID 0xFFE1 or name containing TX
        for info in self.ble.characteristics.values():
            if "ffe1" in info.short_uuid.lower() or "tx" in info.name.lower():
                return info
        for info in self.ble.characteristics.values():
            if "notify" in info.properties or "indicate" in info.properties:
                return info
        return None

    def _find_default_logs_char(self):
        """Finds the notify characteristic for device logs/errors (0xFFE3 / LOGS)."""
        for info in self.ble.characteristics.values():
            if "ffe3" in info.short_uuid.lower() or "log" in info.name.lower():
                return info
        return None

    def on_upload_vm_click(self):
        if not self.vm_compiled:
            self.on_compile_vm_click()
        if not self.vm_compiled:
            return

        if not self.ble.client or not self.ble.client.is_connected:
            self.log("Error: BLE device not connected! Click 'Connect BLE' first.")
            return

        target_info = self.get_selected_info() or self._find_default_rx_char()
        if not target_info:
            self.log("Error: No writable BLE characteristic found.")
            return

        packets_to_send = list(self.vm_compiled.packets)
        asyncio.run_coroutine_threadsafe(
            self._upload_vm_program_task(target_info.cid, packets_to_send), self.loop
        )

    async def _upload_vm_program_task(self, cid: int, packets: list):
        info = self.ble.characteristics.get(cid)
        desc = f"{info.short_uuid} ({info.name})" if info else str(cid)
        self.log(f"--- Uploading VM Program ({len(packets)} packets) to {desc} ---")

        # Auto-subscribe notify on telemetry characteristic if not yet active
        tx_char = self._find_default_tx_char()
        if tx_char and not tx_char.is_monitoring:
            self.log(f"Auto-enabling telemetry monitor on {tx_char.short_uuid} ({tx_char.name})...")
            await self.ble.start_notify(tx_char.cid, self._on_notify)
            self.after(0, self._update_monitor_column, tx_char.cid)

        # Upload packets with 40ms interval (matching runit_guide_test.c)
        for i, pkt in enumerate(packets, start=1):
            try:
                await self.ble.send_bytes(cid, pkt.data)
                self.log(f"  [{i}/{len(packets)}] TX -> {pkt.name} ({len(pkt.data)}B)")
            except Exception as e:
                self.log(f"  [{i}/{len(packets)}] FAILED '{pkt.name}': {e}")
                return
            await asyncio.sleep(0.04)

        self.log(">>> VM Program uploaded & execution started successfully! <<<")

    # Quick Control Handlers
    def on_quick_reset_click(self):
        self._send_quick_packet(encode_vm_reset(), "VM Reset (0x40)")

    def on_quick_stop_click(self):
        self._send_quick_packet(encode_vm_exec(VMExecCommand.STOP), "VM Exec Stop (0x48)")

    def on_quick_start_click(self):
        self._send_quick_packet(encode_vm_exec(VMExecCommand.NORMAL_MODE), "VM Exec Start (0x48)")

    def _send_quick_packet(self, data: bytes, label: str):
        if not self.ble.client or not self.ble.client.is_connected:
            self.log(f"Cannot send {label}: Device not connected.")
            return
        target = self.get_selected_info() or self._find_default_rx_char()
        if not target:
            self.log(f"Cannot send {label}: No writable characteristic.")
            return
        self._send(data)

    # -------------------------------------------------------- Characteristic Selection

    def get_selected_info(self):
        if self.selected_cid is None:
            return None
        return self.ble.characteristics.get(self.selected_cid)

    def on_tree_select(self, _event):
        sel = self.tree.selection()
        if not sel:
            return
        self.selected_cid = int(self.tree.item(sel[0], "values")[0])
        info = self.get_selected_info()
        if info:
            self.target_label_var.set(f"Target: ID {info.cid} - {info.short_uuid} ({info.name})")
            self.btn_toggle_monitor.config(state=tk.NORMAL)
            self.btn_read.config(state=tk.NORMAL if "read" in info.properties else tk.DISABLED)
            self.btn_send_packet.config(state=tk.NORMAL)
            self.btn_send_raw.config(state=tk.NORMAL)
            self.btn_send_batch.config(state=tk.NORMAL if self.batch_lines else tk.DISABLED)

    def refresh_char_list(self):
        self.tree.delete(*self.tree.get_children())
        for info in self.ble.characteristics.values():
            monitor_str = "ON" if info.is_monitoring else "off"
            self.tree.insert(
                "",
                tk.END,
                values=(info.cid, info.short_uuid, info.name, ", ".join(info.properties), monitor_str),
            )

    def _update_monitor_column(self, cid: int):
        for row in self.tree.get_children():
            if int(self.tree.item(row, "values")[0]) == cid:
                info = self.ble.characteristics[cid]
                vals = list(self.tree.item(row, "values"))
                vals[4] = "ON" if info.is_monitoring else "off"
                self.tree.item(row, values=vals)

    # ---------------------------------------------------------- Decoder Form

    def on_class_select(self, _event):
        idx = self.class_cb.current()
        if idx < 0:
            return
        class_val = list(dt.DecoderClass)[idx].value
        structs = self.packets_by_class.get(int(class_val), [])
        self.packet_cb["values"] = [f"0x{int(s._packet_header_):02X}  {s._action_name_}" for s in structs]
        self._current_class_structs = structs
        if self.packet_cb["values"]:
            self.packet_cb.current(0)
            self.on_packet_select(None)

    def on_packet_select(self, _event):
        for w in self.form_frame.winfo_children():
            w.destroy()
        self.field_vars.clear()

        idx = self.packet_cb.current()
        if idx < 0 or not getattr(self, "_current_class_structs", None):
            return
        struct_cls = self._current_class_structs[idx]
        self._current_struct_cls = struct_cls

        for i, (field_name, field_type) in enumerate(struct_cls._fields_):
            array_len = getattr(field_type, "_length_", None)
            label = f"{field_name} [{array_len}]:" if array_len else field_name + ":"
            ttk.Label(self.form_frame, text=label).grid(row=i, column=0, padx=6, pady=3, sticky=tk.E)
            if array_len:
                var = tk.StringVar(value=", ".join(["0"] * array_len))
                ttk.Entry(self.form_frame, textvariable=var, width=28).grid(
                    row=i, column=1, padx=6, pady=3, sticky=tk.W
                )
            elif field_type is ct.c_bool:
                var = tk.StringVar(value="False")
                cb = ttk.Combobox(
                    self.form_frame, textvariable=var, values=["False", "True"], state="readonly", width=26
                )
                cb.grid(row=i, column=1, padx=6, pady=3, sticky=tk.W)
            else:
                var = tk.StringVar(value="0")
                ttk.Entry(self.form_frame, textvariable=var, width=28).grid(
                    row=i, column=1, padx=6, pady=3, sticky=tk.W
                )
            self.field_vars[field_name] = (var, field_type)

        self._update_preview()

    def _build_packet_bytes(self) -> bytes:
        struct_cls = self._current_struct_cls
        inst = struct_cls()
        for field_name, (var, field_type) in self.field_vars.items():
            val_str = var.get().strip()
            array_len = getattr(field_type, "_length_", None)
            if array_len:
                values = [int(v.strip(), 0) for v in val_str.split(",") if v.strip() != ""]
                if len(values) != array_len:
                    raise ValueError(
                        f"'{field_name}' needs {array_len} comma-separated values, got {len(values)}"
                    )
                getattr(inst, field_name)[:] = values
            elif field_type is ct.c_bool:
                setattr(inst, field_name, val_str == "True")
            else:
                setattr(inst, field_name, int(val_str, 0))
        return bytes([int(struct_cls._class_header_), int(struct_cls._packet_header_)]) + bytes(inst)

    def _update_preview(self):
        try:
            packet = self._build_packet_bytes()
            self.preview_var.set(f"Preview: {format_hex(packet)}")
        except Exception as e:
            self.preview_var.set(f"Preview: (invalid) {e}")

    def on_send_packet_click(self):
        try:
            packet = self._build_packet_bytes()
        except Exception as e:
            self.log(f"Error building packet: {e}")
            return
        self._send(packet)

    def on_send_raw_click(self):
        hex_str = self.raw_hex_var.get().strip()
        try:
            data = bytes.fromhex(hex_str.replace(" ", "").replace("0x", ""))
        except ValueError as e:
            self.log(f"Invalid hex string '{hex_str}': {e}")
            return
        self._send(data)

    def _send(self, data: bytes):
        target = self.get_selected_info() or self._find_default_rx_char()
        if not target:
            self.log("Error: No characteristic selected or found.")
            return
        asyncio.run_coroutine_threadsafe(self._send_task(target.cid, data), self.loop)

    async def _send_task(self, cid: int, data: bytes):
        try:
            await self.ble.send_bytes(cid, data)
            info = self.ble.characteristics[cid]
            self.log(f"[TX -> {info.short_uuid} ({info.name})] {format_hex(data)}")
        except Exception as e:
            self.log(f"Send error: {e}")

    # ----------------------------------------------------------- Batch File

    def on_load_file_click(self):
        path = filedialog.askopenfilename(
            title="Select hex packet file", filetypes=[("Text files", "*.txt"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            raw_lines = Path(path).read_text(encoding="utf-8").splitlines()
        except Exception as e:
            self.log(f"Failed to read {path}: {e}")
            return

        self.batch_lines = [
            line.strip() for line in raw_lines if line.strip() and not line.strip().startswith("#")
        ]
        self.batch_status_var.set(f"{Path(path).name}: {len(self.batch_lines)} packet(s) loaded")
        self.batch_preview_text.delete("1.0", tk.END)
        self.batch_preview_text.insert(tk.END, "\n".join(self.batch_lines))
        self.log(f"Loaded {len(self.batch_lines)} packet line(s) from {path}")
        self.btn_send_batch.config(state=tk.NORMAL if self.batch_lines else tk.DISABLED)

    def on_send_batch_click(self):
        target = self.get_selected_info() or self._find_default_rx_char()
        if not target:
            self.log("Error: No characteristic selected or found.")
            return
        if not self.batch_lines:
            self.log("Error: No batch lines loaded.")
            return
        asyncio.run_coroutine_threadsafe(self._send_batch_task(target.cid, list(self.batch_lines)), self.loop)

    async def _send_batch_task(self, cid: int, lines: list[str]):
        info = self.ble.characteristics.get(cid)
        target_desc = f"{info.short_uuid} ({info.name})" if info else str(cid)
        self.log(f"Sending {len(lines)} packet(s) to {target_desc}...")
        sent, failed = 0, 0
        for i, line in enumerate(lines, start=1):
            try:
                data = bytes.fromhex(line.replace(" ", "").replace("0x", ""))
                await self.ble.send_bytes(cid, data)
                self.log(f"  [{i}/{len(lines)}] TX {format_hex(data)}")
                sent += 1
            except Exception as e:
                self.log(f"  [{i}/{len(lines)}] FAILED '{line}': {e}")
                failed += 1
            await asyncio.sleep(0.04)
        self.log(f"Batch complete: {sent} sent, {failed} failed.")

    # -------------------------------------------------------------- Monitor & Notify

    def on_toggle_monitor_click(self):
        info = self.get_selected_info()
        if not info:
            return
        if info.is_monitoring:
            asyncio.run_coroutine_threadsafe(self._stop_monitor_task(info.cid), self.loop)
        else:
            asyncio.run_coroutine_threadsafe(self._start_monitor_task(info.cid), self.loop)

    async def _start_monitor_task(self, cid: int):
        ok = await self.ble.start_notify(cid, self._on_notify)
        info = self.ble.characteristics[cid]
        if ok:
            self.log(f"Monitoring started on {info.short_uuid} ({info.name})")
        else:
            self.log(f"Cannot monitor {info.short_uuid} ({info.name}): no notify/indicate property")
        self.after(0, self._update_monitor_column, cid)

    async def _stop_monitor_task(self, cid: int):
        await self.ble.stop_notify(cid)
        info = self.ble.characteristics[cid]
        self.log(f"Monitoring stopped on {info.short_uuid} ({info.name})")
        self.after(0, self._update_monitor_column, cid)

    def _on_notify(self, cid: int, _char, data: bytes):
        info = self.ble.characteristics.get(cid)
        name = f"{info.short_uuid} ({info.name})" if info else str(cid)
        if info and ("ffe3" in info.short_uuid.lower() or "log" in info.name.lower()):
            text = (
                data[1:].decode("utf-8", errors="replace").strip()
                if len(data) > 1 and data[0] in (0x03, 0x04)
                else data.decode("utf-8", errors="replace").strip()
            )
            self.log(f"[ESP LOG <- {name}] {text}", "info")
        else:
            self.log(f"[RX <- {name}] {format_hex(data)}  ({format_text(data)})", "rx")

    def on_read_click(self):
        info = self.get_selected_info()
        if not info:
            return
        asyncio.run_coroutine_threadsafe(self._read_task(info.cid), self.loop)

    async def _read_task(self, cid: int):
        try:
            data = await self.ble.read_char(cid)
            info = self.ble.characteristics[cid]
            self.log(f"[READ <- {info.short_uuid} ({info.name})] {format_hex(data)}", "rx")
        except Exception as e:
            self.log(f"Read error: {e}")

    # --------------------------------------------------------- Connect / Loop

    def _start_async_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def on_connect_click(self):
        if self.ble.client and self.ble.client.is_connected:
            asyncio.run_coroutine_threadsafe(self._disconnect_task(), self.loop)
        else:
            self.btn_connect.config(state=tk.DISABLED, text="Connecting...")
            self.device_status_var.set("Status: Scanning & Connecting...")
            asyncio.run_coroutine_threadsafe(self._connect_task(), self.loop)

    def on_refresh_click(self):
        asyncio.run_coroutine_threadsafe(self._discover_task(), self.loop)

    async def _connect_task(self):
        self.log(f"Scanning for BLE device '{self.ble.device_name}'...")
        try:
            connected = await self.ble.connect()
            if not connected:
                self.log(f"Device '{self.ble.device_name}' not found.")
                self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Connect BLE"))
                self.after(0, lambda: self.device_status_var.set("Status: Device not found"))
                return

            self.log("Connected. Discovering services & characteristics...")
            await self._discover_task()
            self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Disconnect"))
            self.after(0, lambda: self.btn_refresh.config(state=tk.NORMAL))
            self.after(0, lambda: self.device_status_var.set("Status: Connected"))
        except Exception as e:
            self.log(f"Connection error: {e}")
            self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Connect BLE"))
            self.after(0, lambda: self.device_status_var.set(f"Status: Error ({e})"))

    async def _discover_task(self):
        try:
            await self.ble.discover_services()
            self.log(f"Discovered {len(self.ble.characteristics)} characteristic(s).")
            rx_char = self._find_default_rx_char()
            if rx_char:
                self.selected_cid = rx_char.cid
                self.target_label_var.set(f"Target: ID {rx_char.cid} - {rx_char.short_uuid} ({rx_char.name})")
                self.log(f"Auto-selected RX target: {rx_char.short_uuid} ({rx_char.name}) [ID {rx_char.cid}]")

            # Auto-enable monitoring on TX (telemetry) and LOGS (firmware logs)
            tx_char = self._find_default_tx_char()
            if tx_char and not tx_char.is_monitoring:
                await self.ble.start_notify(tx_char.cid, self._on_notify)
                self.log(f"Auto-subscribed telemetry monitor on {tx_char.short_uuid} ({tx_char.name})")

            logs_char = self._find_default_logs_char()
            if logs_char and not logs_char.is_monitoring:
                await self.ble.start_notify(logs_char.cid, self._on_notify)
                self.log(f"Auto-subscribed firmware log monitor on {logs_char.short_uuid} ({logs_char.name})")

            self.after(0, self.refresh_char_list)
        except Exception as e:
            self.log(f"Discovery error: {e}")

    async def _disconnect_task(self):
        await self.ble.disconnect()
        self.log("Disconnected.")
        self.after(0, lambda: self.btn_connect.config(text="Connect BLE"))
        self.after(0, lambda: self.btn_refresh.config(state=tk.DISABLED))
        self.after(0, lambda: self.device_status_var.set("Status: Disconnected"))
        self.after(0, lambda: self.tree.delete(*self.tree.get_children()))


if __name__ == "__main__":
    app = RunITGUI()
    app.mainloop()
