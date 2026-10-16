// Observe real C++ RendererAPI calls in the managed Web runtime. No renderer
// implementation is replaced. Pixel reads are synchronous and for QA only.
export function observeWebClear() {
  const prototype=WebGL2RenderingContext.prototype, entries=[], states=new WeakMap();
  const original={getParameter:prototype.getParameter,clearColor:prototype.clearColor,clearBufferfv:prototype.clearBufferfv};
  const require=(condition,message)=>{if(!condition)throw new Error(message)};
  const state=gl=>{
    if(!states.has(gl)){const item={queries:0,clears:0,mixed:0,colors:[],lastColor:null,pendingRendererClear:false};states.set(gl,item);entries.push(item)}
    return states.get(gl);
  };
  prototype.getParameter=function(parameter){
    if(parameter===this.COLOR_CLEAR_VALUE)state(this).queries++;
    return original.getParameter.call(this,parameter);
  };
  prototype.clearColor=function(...values){
    const result=original.clearColor.apply(this,values);
    state(this).lastColor=values;state(this).pendingRendererClear=true;
    return result;
  };
  function read(gl,framebuffer,attachment,x,y,integer=false){
    const previous=gl.getParameter(gl.READ_FRAMEBUFFER_BINDING);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER,framebuffer);
    const previousBuffer=gl.getParameter(gl.READ_BUFFER);
    try {
      gl.readBuffer(framebuffer ? attachment : gl.BACK);
      const result=integer ? new Int32Array(1) : new Uint8Array(4);
      gl.readPixels(x,y,1,1,integer ? gl.RED_INTEGER : gl.RGBA,integer ? gl.INT : gl.UNSIGNED_BYTE,result);
      return [...result];
    } finally {
      gl.readBuffer(previousBuffer);gl.bindFramebuffer(gl.READ_FRAMEBUFFER,previous);
    }
  }
  prototype.clearBufferfv=function(buffer,drawBuffer,values,offset=0){
    if(buffer!==this.COLOR || drawBuffer!==0)return original.clearBufferfv.call(this,buffer,drawBuffer,values,offset);
    const gl=this,item=state(gl),source=item.lastColor;
    // WebEditorUI also has explicit attachment clears that do not consume the
    // RendererAPI clear color. Only its immediate SetClearColor/Clear pair is
    // the contract under test, not those independent attachment operations.
    if(!item.pendingRendererClear)return original.clearBufferfv.call(gl,buffer,drawBuffer,values,offset);
    item.pendingRendererClear=false;
    require(source && source.every(Number.isFinite),'Renderer clear must follow a finite SetClearColor in this authoring fixture');
    const expected=source.map(value=>Math.min(1,Math.max(0,value)));
    for(let index=0;index<4;index++)require(Math.abs(values[offset+index]-expected[index])<1e-6,'Web Clear supplied a stale/unclamped color');
    const draw=gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING),viewport=gl.getParameter(gl.VIEWPORT);
    const rect=gl.isEnabled(gl.SCISSOR_TEST) ? gl.getParameter(gl.SCISSOR_BOX) : viewport;
    require(rect[2]>0 && rect[3]>0,'Clear regression needs a nonempty clear rectangle');
    const x=rect[0]+Math.floor(rect[2]/2),y=rect[1]+Math.floor(rect[3]/2);
    let mixed=false;
    if(draw && gl.getFramebufferAttachmentParameter(gl.DRAW_FRAMEBUFFER,gl.COLOR_ATTACHMENT1,gl.FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE)!==gl.NONE)
      mixed=gl.getFramebufferAttachmentParameter(gl.DRAW_FRAMEBUFFER,gl.COLOR_ATTACHMENT1,gl.FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE)===gl.INT;
    if(mixed){
      // Give picking a known value immediately before the production color
      // clear. Restore the draw-buffer mask before invoking native work.
      const buffers=Array.from({length:gl.getParameter(gl.MAX_DRAW_BUFFERS)},(_,index)=>gl.getParameter(gl.DRAW_BUFFER0+index));
      gl.drawBuffers([gl.COLOR_ATTACHMENT0,gl.COLOR_ATTACHMENT1]);
      gl.clearBufferiv(gl.COLOR,1,new Int32Array([1701,0,0,0]));
      gl.drawBuffers(buffers);
      require(read(gl,draw,gl.COLOR_ATTACHMENT1,x,y,true)[0]===1701,'MRT picking sentinel was not written');
    }
    const result=original.clearBufferfv.call(gl,buffer,drawBuffer,values,offset);
    const actual=read(gl,draw,gl.COLOR_ATTACHMENT0,x,y);
    for(let index=0;index<4;index++)require(Math.abs(actual[index]-Math.round(expected[index]*255))<=1,
      `Web Clear GPU pixel differs at channel ${index}: ${actual} vs ${expected}`);
    if(mixed){require(read(gl,draw,gl.COLOR_ATTACHMENT1,x,y,true)[0]===1701,'Web color clear overwrote integer picking');item.mixed++}
    require(gl.getError()===gl.NO_ERROR,'Web color clear/readback generated a GL error');
    item.clears++;item.colors.push(expected);
    return result;
  };
  return {
    snapshot(){return {queries:entries.reduce((sum,item)=>sum+item.queries,0),clears:entries.reduce((sum,item)=>sum+item.clears,0),
      mixed:entries.reduce((sum,item)=>sum+item.mixed,0),colors:entries.flatMap(item=>item.colors)}},
    restore(){Object.assign(prototype,original)},
  };
}
