// Pipe chunks are arbitrary. Fill frames in place rather than repeatedly
// concatenating a growing partial frame (quadratic copying at high FPS).
export class FrameCache {
  constructor(bytes) {this.bytes=bytes;this.latest=null;this.partial=Buffer.allocUnsafe(bytes);this.offset=0;this.retained=new Set();}
  accept(chunk) {
    let offset=0;
    while(offset<chunk.length) {
      const count=Math.min(this.bytes-this.offset,chunk.length-offset);
      chunk.copy(this.partial,this.offset,offset,offset+count);this.offset+=count;offset+=count;
      if(this.offset===this.bytes) {
        const previous=this.latest;this.latest=this.partial;
        this.partial=previous && !this.retained.has(previous)?previous:Buffer.allocUnsafe(this.bytes);this.offset=0;
      }
    }
  }
  retain(frame) {this.retained.add(frame);}
  release(frame) {this.retained.delete(frame);}
  reset() {this.latest=null;this.offset=0;}
}
