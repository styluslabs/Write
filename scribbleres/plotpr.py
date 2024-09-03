import numpy as np
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection

def plotpr(str):
  #v = [[(ii, float(x)) for ii, x in enumerate(line.strip().split())] for line in str if line.strip() and not line.startswith('#')]

  v = [[float(x) for x in line.strip().split()] for line in str if line.strip() and not line.startswith('#')]
  ml = np.mean([len(x) for x in v])

  avgd = {}
  for s in v:
    if(len(s) <= 2*ml):
      avgd.setdefault(len(s), []).append(s)
  z = [np.transpose([np.arange(len(a[0])), np.mean(a, axis=0)]) for a in avgd.values()]

  c = LineCollection(z, linewidths=0.25)

  fig, ax = plt.subplots()
  ax.add_collection(c)
  #ax.autoscale()
  plt.axis([0, 2*ml, 0, 1.1])
  plt.ion()
  plt.show()


def plotfile(filename):
  with open(filename) as f:
    plotpr(f)
  plt.title(filename)


def plot(*args, **kwargs):
  #import matplotlib.pyplot as plt
  plt.ion()  # interactive mode - make plot window non-blocking
  plt.figure()
  plt.plot(*args)
  if 'xlabel' in kwargs: plt.xlabel(kwargs['xlabel'])
  if 'ylabel' in kwargs: plt.ylabel(kwargs['ylabel'])
  if 'title' in kwargs: plt.title(kwargs['title'])
  plt.show()


#plotpr(str.splitlines())  #sys.stdin)

# plotfile('x61.pr')
# plotfile('yoga1.pr')
# plotfile('ipad2.pr')
# plotfile('tpt.pr')
# plotfile('spen.pr')

cpp_code = """
  //createAction("actionDumpPressures", "Dump Pressures", "", "Ctrl+`", [this](){
  //  fprintf(stderr, "# Dumping selected strokes\n\n");
  //  if(!app->activeArea()->currSelection) return;
  //  for(Element* s : app->activeArea()->currSelection->strokes) {
  //    s->dumpPressures();
  //  }
  //  printf("\n");
  //});

//void Element::dumpPressures()
//{
//  if(penPoints.empty())
//    penPoints = toPenPoints();
//  Dim sw = node->getFloatAttr("stroke-width", 1);
//  for(PenPoint& p : penPoints) {
//    Dim pr = 1 - pow(std::max(0.0, 1 - p.dr.dist()/sw), 0.5);
//    printf("%.3f ", pr);
//  }
//  printf("\n");
//}
"""
