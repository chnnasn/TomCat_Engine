// Use the actual native renderer's WebGL context and inherited blend state.
// This diagnostic draws into its own RGBA8 target; it never installs blend
// factors, replaces engine drawing, or depends on CSS screenshot composition.
export function checkWebSourceOverAlpha(gl) {
  const require=(condition,message)=>{if(!condition)throw new Error(message)};
  require(gl && gl.isEnabled(gl.BLEND),'Native renderer must enable blending');
  const factors=[gl.BLEND_SRC_RGB,gl.BLEND_DST_RGB,gl.BLEND_SRC_ALPHA,gl.BLEND_DST_ALPHA].map(p=>gl.getParameter(p));
  require(factors.every((value,index)=>value===[gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA,gl.ONE,gl.ONE_MINUS_SRC_ALPHA][index]),
    `Native source-over factors are incorrect: ${factors}`);
  const previous={draw:gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING),read:gl.getParameter(gl.READ_FRAMEBUFFER_BINDING),
    program:gl.getParameter(gl.CURRENT_PROGRAM),vao:gl.getParameter(gl.VERTEX_ARRAY_BINDING),
    texture:gl.getParameter(gl.TEXTURE_BINDING_2D),viewport:gl.getParameter(gl.VIEWPORT),
    clear:gl.getParameter(gl.COLOR_CLEAR_VALUE),depth:gl.isEnabled(gl.DEPTH_TEST),scissor:gl.isEnabled(gl.SCISSOR_TEST)};
  const framebuffer=gl.createFramebuffer(),texture=gl.createTexture(),vao=gl.createVertexArray(),program=gl.createProgram(),shaders=[];
  const samples=[];
  try {
    for(const [type,source] of [[gl.VERTEX_SHADER,`#version 300 es
      void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}`],
      [gl.FRAGMENT_SHADER,`#version 300 es
      precision highp float;uniform vec4 source;out vec4 color;void main(){color=source;}`]]){
      const shader=gl.createShader(type);shaders.push(shader);gl.shaderSource(shader,source);gl.compileShader(shader);
      require(gl.getShaderParameter(shader,gl.COMPILE_STATUS),gl.getShaderInfoLog(shader));gl.attachShader(program,shader);
    }
    gl.linkProgram(program);require(gl.getProgramParameter(program,gl.LINK_STATUS),gl.getProgramInfoLog(program));
    gl.bindTexture(gl.TEXTURE_2D,texture);gl.texStorage2D(gl.TEXTURE_2D,1,gl.RGBA8,3,3);
    gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
    require(gl.checkFramebufferStatus(gl.FRAMEBUFFER)===gl.FRAMEBUFFER_COMPLETE,'Alpha diagnostic framebuffer incomplete');
    gl.viewport(0,0,3,3);gl.disable(gl.DEPTH_TEST);gl.disable(gl.SCISSOR_TEST);
    gl.bindVertexArray(vao);gl.useProgram(program);
    const source=gl.getUniformLocation(program,'source');
    const draw=color=>{gl.uniform4fv(source,color);gl.drawArrays(gl.TRIANGLES,0,3)};
    const expect=(name,expected)=>{
      const pixel=new Uint8Array(4);gl.readPixels(1,1,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);
      require([...pixel].every((value,index)=>Math.abs(value-expected[index])<=1),`${name}: ${[...pixel]} expected ${expected}`);
      samples.push({name,rgba:[...pixel],expected});
    };
    gl.clearColor(29/255,33/255,45/255,1);gl.clear(gl.COLOR_BUFFER_BIT);
    draw([245/255,245/255,245/255,145/255]);expect('opaque background',[152,154,159,255]);
    gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT);
    draw([245/255,245/255,245/255,145/255]);expect('transparent background',[139,139,139,145]);
    draw([0,1,0,.5]);expect('two translucent layers',[70,197,70,200]);
    require(gl.getError()===gl.NO_ERROR,'Web alpha diagnostic generated a GL error');
    return {factors,samples,contextAttributes:gl.getContextAttributes()};
  } finally {
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER,previous.draw);gl.bindFramebuffer(gl.READ_FRAMEBUFFER,previous.read);
    gl.useProgram(previous.program);gl.bindVertexArray(previous.vao);gl.bindTexture(gl.TEXTURE_2D,previous.texture);
    gl.viewport(...previous.viewport);gl.clearColor(...previous.clear);
    if(previous.depth)gl.enable(gl.DEPTH_TEST);else gl.disable(gl.DEPTH_TEST);
    if(previous.scissor)gl.enable(gl.SCISSOR_TEST);else gl.disable(gl.SCISSOR_TEST);
    for(const shader of shaders)gl.deleteShader(shader);
    gl.deleteProgram(program);gl.deleteVertexArray(vao);gl.deleteTexture(texture);gl.deleteFramebuffer(framebuffer);
  }
}
