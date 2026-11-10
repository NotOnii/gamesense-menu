Menu data pipeline (static RE of the gamesense image, base 0x43310000). Ghidra scripts (FlowDis, LinDis, ...) are not included. strs.js reads skeet-debug Binary/ (set SKEET_BINARY).
1. FlowDis.java 4348704a -> mb2.txt          builder disassembly (opaque "CMP r,imm; JNZ" predicates followed)
2. strs.js -> strs.json                      string per call site: skeet-debug ctx.h int3 contexts (rip -> rax) into menu_fonts.h
3. interp.js mb2.txt strs.json binary menu.json   symbolic run of the builder: tabs, groupboxes, elements, vars, items, deps, callbacks
4. LinDis.java <registration call sites> -> regwin.txt; reginterp.js -> vars.json   config-var defaults
5. gen.js menu.json vars.json binary ../menu_data.h  C++ builder. Callback logic is hand-translated in ../menu_rules.h
tree.txt = readable dump of the recovered menu.
