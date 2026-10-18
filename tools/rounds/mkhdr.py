"""mkhdr.py : matt_types.h (genhdr.py output) + enums/size asserts -> ../matt/matt.h"""
import os
here = os.path.dirname(os.path.abspath(__file__))
top = open(os.path.join(here, 'hdr_top.txt'), encoding='utf8').read()
tail = open(os.path.join(here, 'hdr_tail.txt'), encoding='utf8').read()
body = open(os.path.join(here, 'matt', 'matt_types.h'), encoding='utf8').read()
body = body.replace('struct matt_family_ops *af_ops', 'struct matt_addr_family_ops *af_ops')
body = body.split('\n', 1)[1]
open(os.path.join(here, '..', 'matt', 'matt.h'), 'w', encoding='utf8', newline='\n').write(top + body + tail)
