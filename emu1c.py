import sys

b64alph = list("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")

rom = open(sys.argv[1]).read().strip()
assert(len(rom) % 4 == 0)
assert(all(c in b64alph for c in rom))

rom = "BAAA" + rom

rom = [rom[i:i+4] for i in range(0, len(rom), 4)]

jump_targets = []


cpp_code = open("emu1c.cpp").read()


import socket

for a in sys.argv[1:]:
	if a.startswith("--client="):
		addr, port = a.split("=", 1)[1].split(":")
		addr = socket.gethostbyname(addr)
		addr = [int(q) for q in addr.split(".")]
		addr = addr[0] * 256 * 256 * 256 + addr[1] * 256 * 256 + addr[2] * 256 + addr[3]
		port = int(port)
		cpp_code = cpp_code.replace("REPLACE:NET_PORT", str(port))
		cpp_code = cpp_code.replace("REPLACE:NET_HOST", str(addr))
		cpp_code = "#define NET_CLIENT\n\n" + cpp_code

print(cpp_code[:cpp_code.index("REPLACE:CODE")])
cpp_code = cpp_code[cpp_code.index("REPLACE:CODE")+12:]

s = ""

for ip, instr in enumerate(rom):
	opc, a, b, c = [b64alph.index(q) for q in instr]
	# s += f"/* {str(ip).rjust(4)} {str(opc).rjust(2)} {str(a).rjust(2)} {str(b).rjust(2)} {str(c).rjust(2)} */ "
	if opc == 0:
		s += "{printf(\"halt0\\n\"); running = false;} // halt0\n"
		continue
	cond, op = divmod(opc-1, 21)

	if cond == 0: s += "   "
	if cond == 1: s += "CT~"
	if cond == 2: s += "CF~"

	if op == 20: s += "{printf(\"halt024\\n\"); running = false;} // halt024"

	# Arithmetic and logic
	if op == 0o00: s += f"r[{a}] = (r[{b}] + r[{c}]) & 63;"
	if op == 0o01: s += f"r[{a}] = (r[{b}] + {c}) & 63;"
	if op == 0o02: s += f"r[{a}] = (r[{b}] - r[{c}]) & 63;"
	if op == 0o04: s += f"r[{a}] = r[{b}] | r[{c}];"
	if op == 0o05: s += f"r[{a}] = r[{b}] | c;"
	if op == 0o06: s += f"r[{a}] = r[{b}] ^ r[{c}];"
	if op == 0o07: s += f"r[{a}] = r[{b}] ^ {c};"
	if op == 0o10: s += f"r[{a}] = r[{b}] & r[{c}];"
	if op == 0o11: s += f"r[{a}] = r[{b}] & {c};"
	if op == 0o13: s += f"r[{a}] = (r[{b}] << r[{c}]) & 63;"
	if op == 0o14: s += f"r[{a}] = r[{b}] >> r[{c}];"
	
	# Comparison
	if op == 0o03:
		if (a >> 3) == 0: left, right = f"r[{b}]", f"r[{c}]"
		#if (a >> 3) == 1: left, right = f"r[{c}]", f"r[{b}]"
		if (a >> 3) == 2: left, right = f"r[{b}]", f"{c}"
		if (a >> 3) == 3: left, right = f"{b}", f"r[{c}]"

		s += "cond = "
		if a & 7 == 0: s += f"true;"
		if a & 7 == 1: s += f"false;"
		if a & 7 == 2: s += f"{left} == {right};"
		if a & 7 == 3: s += f"{left} != {right};"
		if a & 7 == 4: s += f"sgn({left}) < sgn({right});"
		if a & 7 == 5: s += f"sgn({left}) > sgn({right});"
		if a & 7 == 6: s += f"{left} < {right};"
		if a & 7 == 7: s += f"{left} > {right};"

	# Shift/rotate by immediate
	if op == 0o12:
		ib = c & 7
		s += f"r[{a}] = "
		if c >> 3 == 0: s += f"(r[{b}] << {ib}) & 63;"
		if c >> 3 == 1: s += f"r[{b}] >> {ib};"
		if c >> 3 == 2: s += f"usgn(sgn(r[{b}]) >> {ib});"
		if c >> 3 == 3: s += f"((r[{b}] << {ib}) | (r[{b}] >> {6-ib})) & 63;"

	# Indirect memory access
	if op == 0o15: s += f"r[{a}] = r[(r[{b}] + {c}) & 63];"
	if op == 0o16: s += f"if(((r[{b}] + {c}) & 63) != 0) r[(r[{b}] + {c}) & 63] = r[{a}];"

	# Fixed-point multiply
	if op == 0o23:
		sh = c & 15
		if c >> 4 == 0: s += f"r[{a}] = ((r[{a}] * r[{b}]) >> {sh}) & 63;"
		if c >> 4 == 1: s += f"r[{a}] = usgn((sgn(r[{a}]) * sgn(r[{b}])) >> {sh});"

	# Control flow
	if op == 0o17:
		s += f"; case~{ip}: // lbl {a*64+b} {c}"
		jump_targets.append((ip, cond, a*64+b, c))
	if op == 0o20: s += f"{{ ip = jup({ip}, cond, {a*64+b}, r[{c}]); break; }}"
	if op == 0o21: s += f"{{ ip = jdn({ip}, cond, {a*64+b}, r[{c}]); break; }}"

	# I/O
	if op == 0o22: s += f"r[{a}] = IO{b}(r[{c}]);"

	s = s.replace("r[0] = ", "")
	s = s.replace(" ","")
	s = s.replace("~"," ")
	s += "\n"
	print(s, end='')
	s = ""

jt = "#define X_JUMP_TARGETS "
for ip, cond, lab, lc in jump_targets:
	jt += f"\\\n\tX_JUMP_TARGET({ip}, {cond}, {lab}, {lc})"

cpp_code = cpp_code.replace("REPLACE:JUMPTARGETS", jt)

print(cpp_code)
