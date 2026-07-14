import os, re

def patch_file(path):
    with open(path) as f:
        lines = f.readlines()
    changed = False
    for i, line in enumerate(lines):
        m = re.search(r'invoke-virtual.*->getSharedPreferences\(Ljava/lang/String;I\)', line)
        if m:
            # Find the mode register: third argument in braces {reg1, reg2, reg3, ...}
            brace = re.search(r'\{([^}]+)\}', line)
            if not brace:
                continue
            regs = [r.strip() for r in brace.group(1).split(',')]
            if len(regs) < 3:
                continue
            mode_reg = regs[2]
            # look backwards for assignment to mode_reg
            for j in range(i-1, max(-1, i-20), -1):
                assign = re.match(rf'^\s*const/4 {re.escape(mode_reg)}, (0x[0-9a-fA-F]+)\s*$', lines[j])
                if assign:
                    val = int(assign.group(1), 16)
                    if val != 0:
                        lines[j] = lines[j].replace(assign.group(1), '0x0')
                        changed = True
                    break
                # stop at method boundary or label
                if re.match(r'^\.\w+', lines[j]):
                    break
    if changed:
        with open(path, 'w') as f:
            f.writelines(lines)
        print('patched', path)

for root, dirs, files in os.walk('work/picovr_smali/smali'):
    for f in files:
        if f.endswith('.smali'):
            patch_file(os.path.join(root, f))
