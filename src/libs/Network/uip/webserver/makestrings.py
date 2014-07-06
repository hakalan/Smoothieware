#!/usr/bin/env python
import re

def stringify(name):
  outc = open(name+'.c','w')
  outh = open(name+'.h','w')
  
  f = open(name)
  
  for line in f:
    match = re.match('(.+) "(.+)"', line)
    if match:
      var,data = match.group(1,2)

      datan = data
      datan = re.sub(r'\\r','\r', datan)
      datan = re.sub(r'\\n','\n', datan)
      datan = re.sub(r'\\01','\01', datan)
      datan = re.sub(r'\\0','\0', datan)
      
      outc.write('const char {}[{}] = \n'.format(var, len(datan) + 1))
      outc.write('/* "' + data + '" */\n')
      outc.write('{')
      outc.write(', '.join('0x{:x}'.format(ord(c)) for c in datan))
      outc.write('};\n')
      outh.write('extern const char {}[{}];\n'.format(var, len(datan) + 1))
  outc.close()
  outh.close()

stringify('http-strings')

