import os
import re

dir = 'httpd-fs'

out = open('httpd-fsdata2.h', 'w')
files = list(os.listdir(dir))

def varname(fname):
  return re.sub(r'\.',r'_',fname)

for fname in files:
  print('Adding file '+fname)
  
  out.write('static const unsigned char data_'+varname(fname)+'[] = {\n')
  out.write('\t/* '+fname+' */\n\t')
  for char in '/'+fname:
    out.write('0x{:02x}, '.format(ord(char)))
  out.write('0,\n\t')
  
  f = open(dir+'/'+fname)

  i = 1
  for line in f:
    for char in line:
      out.write('0x{:02x}, '.format(ord(char)))
      i += 1
      if i>10:
        out.write('\n\t')
        i=1
      
  out.write('0};\n\n')

prevfile = 'NULL'
for fname in files:
  fvar = varname(fname)
  out.write('const struct httpd_fsdata_file file_'+fvar+'[] = {{')
  out.write(prevfile+', data_'+fvar)
  out.write(', data_{} + {}, '.format(fvar, len(fname)+2))
  out.write('sizeof(data_{}) - {}'.format(fvar, len(fname)+3))
  out.write('  }};\n\n')
  prevfile = 'file_'+fvar

out.write('#define HTTPD_FS_ROOT '+prevfile+'\n\n')
out.write('#define HTTPD_FS_NUMFILES {}\n'.format(len(files)))
out.close()
