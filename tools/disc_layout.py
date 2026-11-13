"""Extract only unencrypted disc metadata from a local single-file WBFS image."""
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]


def prepare() -> None:
    images=list((ROOT/"asset").glob("*.wbfs"))
    if len(images)!=1: raise ValueError("Expected one WBFS source for this local boot recipe")
    with images[0].open("rb") as stream:
        header=stream.read(512)
        if len(header)!=512 or header[:4]!=b"WBFS" or not header[12]: raise ValueError("Invalid WBFS header/slot")
        if not 9<=header[8]<=16 or not 15<=header[9]<=26: raise ValueError("Unsupported WBFS sector shifts")
        hd,block=1<<header[8],1<<header[9]
        if struct.unpack_from(">I",header,4)[0]*hd!=images[0].stat().st_size: raise ValueError("Split or truncated WBFS unsupported by metadata recipe")
        count=(286864*0x8000+block-1)//block
        stream.seek(hd+256); mapping=stream.read(count*2)
        if len(mapping)!=count*2: raise ValueError("Truncated WBFS map")
        table=struct.unpack(">"+"H"*count,mapping)
        def read(offset,size):
            result=bytearray()
            while size:
                sector,index=divmod(offset,block)
                if sector>=len(table) or not table[sector]: raise ValueError("Metadata references an unavailable WBFS block")
                take=min(size,block-index)
                physical=table[sector]*block+index
                if physical+take>images[0].stat().st_size: raise ValueError("WBFS block outside source file")
                stream.seek(physical); data=stream.read(take)
                if len(data)!=take: raise ValueError("Short WBFS read")
                result+=data; offset+=take; size-=take
            return bytes(result)
        system=read(0,0x50000)
        if system[:6]!=b"RK5E01" or system[7]!=0: raise ValueError("WBFS disc version mismatch")
        partitions=[]
        largest=0
        for group in range(4):
            n,table_words=struct.unpack_from(">II",system,0x40000+group*8)
            if n>64: raise ValueError("Partition count exceeds bound")
            if not n: continue
            entries=read(table_words*4,n*8)
            for offset_words,kind in struct.iter_unpack(">II",entries):
                base=offset_words*4
                data_offset,data_size=struct.unpack(">II",read(base+0x2B8,8))
                largest=max(largest,base+(data_offset+data_size)*4)
                if kind==0: partitions.append(base)
        if len(partitions)!=1: raise ValueError("Expected one game partition")
        capacity=4699979776 if largest<=4707319808 else 8511160320
        if largest>capacity: raise ValueError("Disc references exceed selected retail capacity")
        out=ROOT/"private/boot"; out.mkdir(parents=True,exist_ok=True)
        (out/"system-area.bin").write_bytes(system)
        (out/"geometry.bin").write_bytes(struct.pack(">4sIQQ",b"KRD0",1,capacity,partitions[0]))


if __name__=="__main__": prepare()
