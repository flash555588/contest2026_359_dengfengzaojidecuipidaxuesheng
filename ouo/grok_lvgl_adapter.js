// SPDX-License-Identifier: Apache-2.0
// A small in-memory SVG scene adapter. Upstream geometry and animation stay
// unchanged; contours are rendered by bounded native LVGL polygon widgets.
var window = globalThis;
var GrokPort = (function () {
  var registry = {}, root, scale, originX, originY, mountMatrix, frameStack = null, renderOrder=[], lastOrder=[], showHandles=[];
  var packed=typeof ui.polygonBuffer==='function'&&typeof ui.polygonSetBuffer==='function';
  function Node(tag) { this.tag=tag;this.attrs={};this.style={};this.children=[];this.handles=[];this.paint=[];this.buffers=[];this.hidden=[];this.revision=0;this.visibleHandles=0;this.parentNode=null; }
  Node.prototype.setAttribute=function(k,v) {
    // Even an equal assignment may accompany a direct style.display edit in
    // the upstream renderer. Restart a pending traversal as before; only the
    // geometry/style revision is conditional on the attribute changing.
    frameStack=null;v=String(v);if(this.attrs[k]===v)return;
    if(k==='id'){if(this.attrs.id)delete registry[this.attrs.id];registry[v]=this;}
    this.attrs[k]=v;this.revision++;
    if(this.tag==='stop'&&this.parentNode)this.parentNode.revision++;
  };
  Node.prototype.getAttribute=function(k) { return this.attrs[k]===undefined?null:this.attrs[k]; };
  Node.prototype.appendChild=function(n) { n.parentNode=this;this.children.push(n);this.revision++;frameStack=null;return n; };
  Node.prototype.removeChild=function(n) {
    frameStack=null;
    var i=this.children.indexOf(n);if(i>=0){this.children.splice(i,1);this.revision++;}
    function dispose(p) { p.children.forEach(dispose);p.handles.forEach(function(h){ui.remove(h);});p.handles=[];p.paint=[];p.buffers=[];p.hidden=[];p.lastRender=null;if(p.attrs.id)delete registry[p.attrs.id]; }
    // Native handles reuse free slots. The same numeric order after a remove
    // and create need not mean the same LVGL objects or their painter order.
    lastOrder=[];
    dispose(n);n.parentNode=null;return n;
  };
  Node.prototype.remove=function() { if(this.parentNode)this.parentNode.removeChild(this); };
  function mul(a,b) { return [a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3],a[0]*b[4]+a[2]*b[5]+a[4],a[1]*b[4]+a[3]*b[5]+a[5]]; }
  function transform(s) {
    var m=[1,0,0,1,0,0],re=/(translate|rotate|scale)\(([^)]*)\)/g,v;
    while((v=re.exec(s||''))) {
      var p=v[2].trim().split(/[ ,]+/).map(Number),n;
      if(v[1]==='translate')n=[1,0,0,1,p[0],p[1]||0];
      else if(v[1]==='scale')n=[p[0],0,0,p.length>1?p[1]:p[0],0,0];
      else {var a=p[0]*Math.PI/180;n=[Math.cos(a),Math.sin(a),-Math.sin(a),Math.cos(a),0,0];}
      m=mul(m,n);
    }
    return m;
  }
  function color(s) {
    if(!s||s==='none')return 0;
    if(s.indexOf('url(')===0) {var g=registry[s.slice(5,-1)];return g&&g.children.length?color(g.children[Math.floor(g.children.length/2)].attrs['stop-color']):0;}
    if(s[0]==='#') {var h=s.slice(1);if(h.length===3)h=h.split('').map(function(c){return c+c;}).join('');return parseInt(h,16);}
    if(s.indexOf('hsl')===0) {
      var n=s.match(/[-\d.]+/g).map(Number),h=((n[0]%360)+360)%360/60,sat=n[1]/100,l=n[2]/100;
      var c=(1-Math.abs(2*l-1))*sat,x=c*(1-Math.abs(h%2-1)),t=l-c/2;
      var p=h<1?[c,x,0]:h<2?[x,c,0]:h<3?[0,c,x]:h<4?[0,x,c]:h<5?[x,0,c]:[c,0,x];
      return (Math.round((p[0]+t)*255)<<16)|(Math.round((p[1]+t)*255)<<8)|Math.round((p[2]+t)*255);
    }
    return 0;
  }
  function paths(s) {
    var t=(s||'').match(/[MLAZ]|[-+]?(?:\d*\.)?\d+(?:e[-+]?\d+)?/gi)||[],out=[],p=[],i=0;
    while(i<t.length) {
      var op=t[i++];
      if(op==='M') {if(p.length>=3)out.push(p);p=[[Number(t[i++]),Number(t[i++])]];}
      else if(op==='L')p.push([Number(t[i++]),Number(t[i++])]);
      else if(op==='A') {
        i+=5;var end=[Number(t[i++]),Number(t[i++])],start=p[p.length-1];
        if(start) {var cx=(start[0]+end[0])/2,cy=(start[1]+end[1])/2,r=Math.hypot(start[0]-cx,start[1]-cy),a=Math.atan2(start[1]-cy,start[0]-cx);
          for(var j=1;j<=6;j++)p.push([cx+r*Math.cos(a-Math.PI*j/6),cy+r*Math.sin(a-Math.PI*j/6)]);}
      } else if(op==='Z') {if(p.length>=3)out.push(p);p=[];}
    }
    if(p.length>=3)out.push(p);return out;
  }
  function contours(n) {
    var a=n.attrs;
    if(n.tag==='path') {
      if(!n.pathContours||n.pathText!==a.d){n.pathText=a.d;n.pathContours=paths(a.d);}
      return n.pathContours;
    }
    var shape=[a.x,a.y,a.width,a.height,a.r];
    if(same(n.shape,shape)&&n.cachedContours)return n.cachedContours;
    var result=[];
    if(n.tag==='rect') {var x=Number(a.x)||0,y=Number(a.y)||0,w=Number(a.width)||0,h=Number(a.height)||0;result=[[[x,y],[x+w,y],[x+w,y+h],[x,y+h]]];}
    if(n.tag==='circle') {var p=[],r=Number(a.r)||0;for(var i=0;i<16;i++)p.push([Math.cos(i*Math.PI/8)*r,Math.sin(i*Math.PI/8)*r]);result=[p];}
    n.shape=shape;n.cachedContours=result;return result;
  }
  function same(a,b) {if(a===b)return true;if(!a||!b||a.length!==b.length)return false;for(var i=0;i<a.length;i++)if(a[i]!==b[i])return false;return true;}
  function samePaint(a,b) {
    if(!a||a.color!==b.color||a.opacity!==b.opacity||a.width!==b.width||!same(a.gradient,b.gradient)||a.points.length!==b.points.length)return false;
    for(var i=0;i<a.points.length;i++)if(!same(a.points[i],b.points[i]))return false;
    return true;
  }
  function hide(n,k) {if(!n.hidden[k]){ui.setHidden(n.handles[k],true);n.hidden[k]=true;}}
  function collect(n) {
    for(var k=0;k<n.visibleHandles;k++) {
      renderOrder.push(n.handles[k]);
      if(n.hidden[k])showHandles.push([n,k]);
    }
  }
  function gradient(fill,m,points,flat) {
    if(fill.indexOf('url(')!==0)return null;
    var g=registry[fill.slice(5,-1)];if(!g||g.children.length<2)return null;
    var a=g.attrs,x1,y1,x2=0,y2=0,r=0;
    if(g.tag==='radialGradient') {
      var left=Infinity,top=Infinity,right=-Infinity,bottom=-Infinity;
      for(var i=0;i<points.length;i+=flat?2:1){var x=flat?points[i]:points[i][0],y=flat?points[i+1]:points[i][1];if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;}
      var w=right-left,h=bottom-top;
      x1=left+w*parseFloat(a.cx)/100;y1=top+h*parseFloat(a.cy)/100;
      r=Math.sqrt((w*w+h*h)/2)*parseFloat(a.r)/100;
    } else {
      var gx1=Number(a.x1)||0,gy1=Number(a.y1)||0,gx2=Number(a.x2)||0,gy2=Number(a.y2)||0;
      x1=m[0]*gx1+m[2]*gy1+m[4];y1=m[1]*gx1+m[3]*gy1+m[5];
      x2=m[0]*gx2+m[2]*gy2+m[4];y2=m[1]*gx2+m[3]*gy2+m[5];
    }
    var result=[g.tag==='radialGradient'?1:2,g.children.length,x1,y1,x2,y2,r];
    g.children.forEach(function(s){var offset=s.attrs.offset||'0';result.push(parseFloat(offset)/(offset.indexOf('%')>=0?100:1),color(s.attrs['stop-color']));});
    return result;
  }
  function draw(n,m,opacity,visible) {
    if(n.tag==='defs'||n.tag==='radialGradient'||n.tag==='linearGradient'||n.tag==='stop')return;
    var transformChanged=n.transformText!==n.attrs.transform||!n.transformMatrix;
    if(transformChanged){n.transformText=n.attrs.transform;n.transformMatrix=transform(n.attrs.transform);}
    if(transformChanged||!same(n.parentMatrix,m)||!n.worldMatrix){n.parentMatrix=m;n.worldMatrix=mul(m,n.transformMatrix);}
    m=n.worldMatrix;
    opacity*=n.attrs.opacity===undefined?1:Number(n.attrs.opacity);
    visible=visible&&n.style.display!=='none'&&opacity>0.001;
    var a=n.attrs,fill=a.fill||'none',stroke=fill==='none'?(a.stroke||'none'):'none';
    if(n.tag!=='path'&&n.tag!=='rect'&&n.tag!=='circle'&&n.tag!=='text'){
      for(var child=n.children.length-1;child>=0;child--)frameStack.push([n.children[child],m,opacity,visible]);
      return;
    }
    var g=fill.indexOf('url(')===0?registry[fill.slice(5,-1)]:null;
    var state=[n.revision,m[0],m[1],m[2],m[3],m[4],m[5],opacity,visible,n.textContent,g,g&&g.revision];
    if(same(n.lastRender,state)) {
      // A sibling's animation must not invalidate an unchanged body or glyph.
      collect(n);
    } else if(n.tag==='text') {
      if(!n.handles.length){n.handles.push(ui.text(n.textContent||'',0,0,20,color(fill)));lastOrder=[];}
      var h=n.handles[0];n.visibleHandles=visible?1:0;
      if(!visible)hide(n,0);
      if(visible) {
        if(n.lastText!==n.textContent){ui.setText(h,n.textContent||'');n.lastText=n.textContent;}
        ui.setPos(h,Math.round(m[4]-8),Math.round(m[5]-16));ui.setColor(h,color(fill));ui.setOpacity(h,Math.round(opacity*255));
        var fontSize=Math.min(48,Math.max(14,Math.round((Number(a['font-size'])||18)*scale)));
        if(n.fontSize!==fontSize){ui.setStyle(h,{fontSize:fontSize});n.fontSize=fontSize;}
      }
      collect(n);
    } else {
      var polygons=visible&&(fill!=='none'||stroke!=='none')?contours(n):[];
      n.visibleHandles=polygons.length;
      for(var k=0;k<polygons.length;k++) {
        var contour=polygons[k];if(contour.length>128)throw new Error('SVG contour exceeds LVGL bound');
        var points,changed=false;
        if(packed){
          points=n.buffers[k];if(!points||points.length!==contour.length*2){points=n.buffers[k]=new Int32Array(contour.length*2);changed=true;}
          for(var i=0;i<contour.length;i++){
            var p=contour[i],x=Math.round(m[0]*p[0]+m[2]*p[1]+m[4]),y=Math.round(m[1]*p[0]+m[3]*p[1]+m[5]);
            if(points[i*2]!==x||points[i*2+1]!==y){points[i*2]=x;points[i*2+1]=y;changed=true;}
          }
        }else points=contour.map(function(p){return [Math.round(m[0]*p[0]+m[2]*p[1]+m[4]),Math.round(m[1]*p[0]+m[3]*p[1]+m[5])];});
        var c=color(fill==='none'?stroke:fill),opa=Math.round(opacity*255),width=stroke==='none'?0:Math.max(1,Math.round((Number(a['stroke-width'])||1)*scale));
        var grad=gradient(fill,m,points,packed);
        var paint={points:points,color:c,opacity:opa,width:width,gradient:grad};
        var previous=n.paint[k];
        if(!n.handles[k]){n.handles[k]=packed?ui.polygonBuffer(points.buffer,c,opa,width,grad):ui.polygon(points,c,opa,width,grad);lastOrder=[];hide(n,k);}
        else if(packed){if(changed||!previous||previous.color!==c||previous.opacity!==opa||previous.width!==width||!same(previous.gradient,grad))ui.polygonSetBuffer(n.handles[k],points.buffer,c,opa,width,grad);}
        else if(!samePaint(previous,paint))ui.polygonSet(n.handles[k],points,c,opa,width,grad);
        n.paint[k]=paint;
      }
      for(var j=polygons.length;j<n.handles.length;j++)hide(n,j);
      collect(n);
    }
    n.lastRender=state;
    for(var child=n.children.length-1;child>=0;child--)
      frameStack.push([n.children[child],m,opacity,visible]);
  }
  return {
    clock:0,Node:Node,
    mount:function(width,height) {root=new Node('mount');scale=Math.min(height*0.56/228,width/420);originX=width/2-114.2705*scale;originY=height*0.43-114.2705*scale;mountMatrix=[scale,0,0,scale,originX,originY];return root;},
    isRendering:function(){return !!frameStack;},
    sync:function() {
      if(!root)return true;
      if(!frameStack){frameStack=[[root,mountMatrix,1,true]];renderOrder=[];showHandles=[];}
      var now=typeof ui.nowMs==='function'?ui.nowMs:function(){return 0;},began=now();
      // Preserve traversal/z order while sharing the UI thread between slices.
      do {var node=frameStack.pop();draw(node[0],node[1],node[2],node[3]);}
      while(frameStack.length&&now()-began<15);
      if(!frameStack.length) {
        // Restore SVG painter order only when topology/visibility changes.
        // New/revealed contours stay hidden until this commit, so a yielded
        // rear ribbon cannot temporarily cover the body or the front ribbon.
        if(!same(lastOrder,renderOrder)){renderOrder.forEach(function(h){ui.front(h);});lastOrder=renderOrder;}
        showHandles.forEach(function(pair){ui.setHidden(pair[0].handles[pair[1]],false);pair[0].hidden[pair[1]]=false;});
        frameStack=null;showHandles=[];
      }
      return !frameStack;
    }
  };
})();
var document={createElementNS:function(ns,tag){return new GrokPort.Node(tag);}};
var performance={now:function(){return GrokPort.clock;}};
// Native app owns a single 50 ms timer. Upstream shared browser rAF is unused.
var requestAnimationFrame=function(){return 0;};
