import sys
L=open(sys.argv[1]+'/probe.log').read().split('\n')
ev=[]
for l in L:
    p=l.split()
    if l.startswith('MARK'): ev.append(('M',float(p[1]),p[2],int(p[3])))
    elif l.startswith('FRAME '): ev.append(('F',float(p[1]),int(p[2])))
names=['Google search','YouTube home','BBC News','GitHub repo','Hacker News']
print('%-14s %8s %9s %8s %9s'%('page','ready s','load KB','scr fps','scr KB/s'))
for i in range(5):
    t={m[2]:m[1] for m in ev if m[0]=='M' and m[3]==i}
    ld=[e for e in ev if e[0]=='F' and t['nav']<=e[1]<t['scroll']]
    sc=[e for e in ev if e[0]=='F' and t['scroll']<=e[1]<t['end']]
    tot=sum(e[2] for e in ld); cum=0; ready=0
    for e in ld:
        cum+=e[2]
        if cum>=0.9*tot: ready=e[1]-t['nav']; break
    dt=t['end']-t['scroll']
    print('%-14s %8.1f %9.0f %8.2f %9.1f'%(names[i],ready,tot/1024,len(sc)/dt,sum(e[2] for e in sc)/1024/dt))
