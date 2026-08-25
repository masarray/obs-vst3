from pathlib import Path
# Align the deterministic replacement text with the exact S1.4 main source.
p=Path('scripts/apply_s1_5_engine.py')
s=p.read_text(encoding='utf-8')
old='''        // actions remain deliberately unsupported until their individual S1\n        // tracer bullets implement them at a quiesced lifecycle frontier.\n        if (restart_plan.reload_component || restart_plan.reconfigure_io ||\n'''
new='''        // S1.1 makes every requested transaction explicit. The following\n        // actions remain deliberately unsupported until their individual S1\n        // tracer bullets implement them at a quiesced lifecycle frontier.\n        if (restart_plan.reload_component || restart_plan.reconfigure_io ||\n'''
if s.count(old)!=1:
    raise RuntimeError(f'expected one restart marker in patch script, got {s.count(old)}')
p.write_text(s.replace(old,new,1),encoding='utf-8',newline='\n')
