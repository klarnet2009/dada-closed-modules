M=(1<<64)-1
P1=0x9E3779B185EBCA87;P2=0xC2B2AE3D27D4EB4F;P3=0x165667B19E3779F9;P4=0x85EBCA77C2B2AE63;P5=0x27D4EB2F165667C5
def rotl(x,r): return ((x<<r)|(x>>(64-r)))&M
def rnd(a,i): a=(a+i*P2)&M; a=rotl(a,31); return (a*P1)&M
def mrg(a,v): v=rnd(0,v); a^=v; return (a*P1+P4)&M
def xxh64(b,seed=0):
    n=len(b);p=0
    if n>=32:
        v=[(seed+P1+P2)&M,(seed+P2)&M,seed,(seed-P1)&M]
        while p<=n-32:
            for i in range(4):
                v[i]=rnd(v[i],int.from_bytes(b[p:p+8],'little'));p+=8
        h=(rotl(v[0],1)+rotl(v[1],7)+rotl(v[2],12)+rotl(v[3],18))&M
        for i in range(4): h=mrg(h,v[i])
    else: h=(seed+P5)&M
    h=(h+n)&M
    while p<=n-8:
        k=rnd(0,int.from_bytes(b[p:p+8],'little')); h^=k; h=(rotl(h,27)*P1+P4)&M; p+=8
    if p<=n-4:
        h^=(int.from_bytes(b[p:p+4],'little')*P1)&M; h=(rotl(h,23)*P2+P3)&M; p+=4
    while p<n:
        h^=(b[p]*P5)&M; h=(rotl(h,11)*P1)&M; p+=1
    h^=h>>33; h=(h*P2)&M; h^=h>>29; h=(h*P3)&M; h^=h>>32
    return h
def kh(s): return xxh64(s.encode())&0xffffffff
if __name__=="__main__":
    print(hex(kh("_ZTSFvP10dlcompressE")), "expect 0xecf127c5")
    print(hex(kh("_ZTSFP10dlcompressiiE")), "expect 0x968a86a")
