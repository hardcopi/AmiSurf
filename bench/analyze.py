import sys,re
d=sys.argv[1]; L=open(d+'/host.log',errors='replace').read().split('\n')
marks=[l.split() for l in open(d+'/marks')]
names=['Google search','YouTube home','BBC News','GitHub repo','Hacker News']
def stats(a,b):
    r=[]
    for l in L[a:b]:
        m=re.search(r'cmd stat fr=(\d+) bytes=(\d+) dtms=(\d+) cms=(\d+) ready=(-?\d+)',l)
        if m: r.append(tuple(map(int,m.groups())))
    return r
out=[]
for i in range(5):
    p=int(marks[i*3][2]); s=int(marks[i*3+1][2]); e=int(marks[i*3+2][2])
    ld=stats(p,s); sc=stats(s,e)
    tot=sum(x[1] for x in ld); cum=0; ready=-1
    for x in ld:
        cum+=x[1]
        if tot and cum>=0.9*tot: ready=x[4]; break
    fr=sum(x[0] for x in sc); dt=sum(x[2] for x in sc) or 1; by=sum(x[1] for x in sc)
    cms=(sum(x[0]*x[3] for x in sc)/fr/10) if fr else 0
    lfr=sum(x[0] for x in ld); lcms=(sum(x[0]*x[3] for x in ld)/lfr/10) if lfr else 0
    out.append((names[i],ready/1000,tot/1024,fr*1000/dt,by/1024/(dt/1000),cms,lfr,lcms))
print('%-14s %8s %9s %8s %9s %10s %7s %9s'%('page','ready s','load KB','scr fps','scr KB/s','scr ms/fr','ld frm','ld ms/fr'))
for o in out: print('%-14s %8.1f %9.0f %8.2f %9.1f %10.1f %7d %9.1f'%o)
