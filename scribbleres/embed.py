#!/usr/bin/python3
# generate C++ source file from list of text files

# usage: python embed.py icons/*.svg > res_icons.cpp
# `chmod 555 res_icons.cpp` might be a good idea

# Alternative approaches:
# - https://stackoverflow.com/questions/4158900/embedding-resources-in-executable-using-gcc
# - https://stackoverflow.com/questions/4864866/c-c-with-gcc-statically-add-resource-files-to-executable-library
# - https://github.com/graphitemaster/incbin (Macro for using .incbin)
# inline assembly with .incbin would be my preferred approach, except that it doesn't work with MSVC

import sys
import subprocess

sys.argv.pop(0)
compress = False
if sys.argv[0] == "--compress":
  compress = True
  sys.argv.pop(0)

c_out = ['// Generated by embed.py\n\n']
h_out = []  #['#pragma once\n\n']
for filename in sys.argv:
  varname = filename.replace('.', '_').replace('/', '__').replace('\\', '__').replace('-', '_')
  if compress:
    h_out.append('{"%s", {%s, sizeof(%s)}}' % (filename, varname, varname))
    c_out.append('static const unsigned char ' + varname + '[] = {\n')
    # replacements are to try to avoid whitespace changes if file is opened and saved in an editor
    c_out.append(subprocess.check_output("gzip -c %s | xxd -i" % filename, shell=True, encoding='UTF-8'))
    c_out.append('};\n\n')
  else:
    h_out.append('{"' + filename + '", ' + varname + '}')
    with open(filename, 'r') as f:
      c_out.append('static const char* ' + varname + ' = R"~~~~(')
      # replacements are to try to avoid whitespace changes if file is opened and saved in an editor
      c_out.append(f.read().replace("\t", "  ").replace("\r\n", "\n"))
      c_out.append(')~~~~";\n\n')

c_out.append('static void LOAD_RES_FN() { addStringResources({\n  %s\n}); }\n' % ',\n  '.join(h_out))
c_out.append('#undef LOAD_RES_FN\n')

print(''.join(c_out))
