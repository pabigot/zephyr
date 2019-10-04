// Replace legacy time conversion API with new equivalents.
// spatch --sp-file hack.cocci --very-quiet --dir . --include-headers | patch -p1

@expression@
expression E;
@@
-__ticks_to_ms(E)
+k_ticks_to_ms_floor64(E)

@expression@
expression E;
@@
-z_ms_to_ticks(E)
+k_ms_to_ticks_ceil32(E)

@expression@
expression E;
@@ // wrong should be floor32
-__ticks_to_us(E)
+k_ticks_to_us_floor32(E)

@expression@
@@
-sys_clock_hw_cycles_per_tick()
+k_ticks_to_cyc_floor32(1)

@expression@
expression E;
@@
-SYS_CLOCK_HW_CYCLES_TO_NS64(E)
+k_cyc_to_ns_floor64(E)


@expression@
expression E;
@@
-SYS_CLOCK_HW_CYCLES_TO_NS(E)
+k_cyc_to_ns_floor32(E)

@expression@
expression E;
@@
-z_us_to_ticks(E)
+k_us_to_ticks_ceil32(E)
