import sys
from PIL import Image, ImageStat
for f in sys.argv[1:]:
  try: im=Image.open(f).convert('RGB').crop((0,0,960,540))
  except Exception as e: print(f,'missing'); continue
  s=ImageStat.Stat(im); print(f,'mean %.1f stddev %.1f'%(sum(s.mean)/3,sum(s.stddev)/3))
