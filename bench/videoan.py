import sys,re
d=sys.argv[1]
try:
    L=open(d+'/probe.log').read().split('\n'); t={}; fr=[]
    for l in L:
        p=l.split()
        if l.startswith('MARK'): t[p[2]]=float(p[1])
        elif l.startswith('FRAME '): fr.append((float(p[1]),int(p[2])))
    w=[f for f in fr if t['scroll']<=f[0]<t['end']]; dt=t['end']-t['scroll']
    print('%7.2f fps %8.1f KB/s %8s'%(len(w)/dt,sum(f[1] for f in w)/1024/dt,'-'))
except FileNotFoundError:
    L=open(d+'/host.log',errors='replace').read().split('\n'); m=[int(l.split()[1]) for l in open(d+'/marks')]
    r=[tuple(map(int,x.groups())) for x in (re.search(r'cmd stat fr=(\d+) bytes=(\d+) dtms=(\d+) cms=(\d+)',l) for l in L[m[0]:m[1]]) if x]
    fr=sum(x[0] for x in r); dt=sum(x[2] for x in r) or 1; by=sum(x[1] for x in r); cms=sum(x[0]*x[3] for x in r)/fr/10 if fr else 0
    print('%7.2f fps %8.1f KB/s %8.1f ms/fr'%(fr*1000/dt,by/1024/(dt/1000),cms))
