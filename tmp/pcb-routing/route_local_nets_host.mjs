const bridgeUrl = "http://127.0.0.1:49620";
const grid = 10;
const board = { minX: 0, maxX: 4409.4488, minY: -2677.1654, maxY: 0 };
const directions = [
  [1, 0],
  [1, 1],
  [0, 1],
  [-1, 1],
  [-1, 0],
  [-1, -1],
  [0, -1],
  [1, -1],
];

const targets = process.argv.slice(2).map((value) => {
  const parts = value.split(":");
  const net = parts[0];
  const width = Number(parts[1]);
  if (!net || !Number.isFinite(width) || width <= 0) throw new Error(`Invalid target: ${value}`);
  if (parts.length === 7) {
    const [layer, startX, startY, endX, endY] = parts.slice(2).map(Number);
    if (![layer, startX, startY, endX, endY].every(Number.isFinite)) throw new Error(`Invalid explicit target: ${value}`);
    return { net, width, layer, start: { x: startX, y: startY }, end: { x: endX, y: endY } };
  }
  if (parts.length !== 2) throw new Error(`Invalid target: ${value}`);
  return { net, width };
});

if (!targets.length) {
  throw new Error("Usage: node route_local_nets_host.mjs NET:WIDTH [...]");
}

const windowsResponse = await fetch(`${bridgeUrl}/eda-windows`);
const windowsPayload = await windowsResponse.json();
const windowId = windowsPayload.activeWindowId || windowsPayload.windows?.[0]?.windowId;
if (!windowId) throw new Error("No EasyEDA window is connected");

async function execute(code) {
  const response = await fetch(`${bridgeUrl}/execute`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ windowId, code }),
  });
  const payload = await response.json();
  if (!payload.success) throw new Error(payload.error || "EasyEDA execute failed");
  return payload.result;
}

const geometry = await execute(
  "const pads=await eda.pcb_PrimitivePad.getAll(); const lines=(await eda.pcb_PrimitiveLine.getAll()).filter(x=>x.net&&[1,2,15,16].includes(x.layer)); const vias=await eda.pcb_PrimitiveVia.getAll(); const regions=await eda.pcb_PrimitiveRegion.getAll(); return {pads,lines,vias,regions};",
);

function segmentIntersectsBox(a, b, box) {
  let t0 = 0;
  let t1 = 1;
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  for (const [p, q] of [
    [-dx, a.x - box.minX],
    [dx, box.maxX - a.x],
    [-dy, a.y - box.minY],
    [dy, box.maxY - a.y],
  ]) {
    if (Math.abs(p) < 1e-9) {
      if (q < 0) return false;
      continue;
    }
    const t = q / p;
    if (p < 0) {
      if (t > t1) return false;
      if (t > t0) t0 = t;
    } else {
      if (t < t0) return false;
      if (t < t1) t1 = t;
    }
  }
  return t1 >= t0;
}

function pointSegmentDistance(point, start, end) {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  const squaredLength = dx * dx + dy * dy;
  if (squaredLength < 1e-9) return Math.hypot(point.x - start.x, point.y - start.y);
  const position = Math.max(
    0,
    Math.min(1, ((point.x - start.x) * dx + (point.y - start.y) * dy) / squaredLength),
  );
  return Math.hypot(point.x - (start.x + position * dx), point.y - (start.y + position * dy));
}

function orientation(a, b, c) {
  const value = (b.y - a.y) * (c.x - b.x) - (b.x - a.x) * (c.y - b.y);
  if (Math.abs(value) < 1e-9) return 0;
  return value > 0 ? 1 : 2;
}

function onSegment(a, b, c) {
  return (
    b.x <= Math.max(a.x, c.x) + 1e-9 &&
    b.x + 1e-9 >= Math.min(a.x, c.x) &&
    b.y <= Math.max(a.y, c.y) + 1e-9 &&
    b.y + 1e-9 >= Math.min(a.y, c.y)
  );
}

function segmentsIntersect(a, b, c, d) {
  const o1 = orientation(a, b, c);
  const o2 = orientation(a, b, d);
  const o3 = orientation(c, d, a);
  const o4 = orientation(c, d, b);
  if (o1 !== o2 && o3 !== o4) return true;
  if (o1 === 0 && onSegment(a, c, b)) return true;
  if (o2 === 0 && onSegment(a, d, b)) return true;
  if (o3 === 0 && onSegment(c, a, d)) return true;
  if (o4 === 0 && onSegment(c, b, d)) return true;
  return false;
}

function segmentDistance(a, b, c, d) {
  if (segmentsIntersect(a, b, c, d)) return 0;
  return Math.min(
    pointSegmentDistance(a, c, d),
    pointSegmentDistance(b, c, d),
    pointSegmentDistance(c, a, b),
    pointSegmentDistance(d, a, b),
  );
}

function padBox(pad, margin) {
  if (pad.pad?.[0] === "POLYGON" && Array.isArray(pad.pad[1])) {
    const coordinates = pad.pad[1].filter((value) => typeof value === "number");
    const xs = [];
    const ys = [];
    for (let index = 0; index + 1 < coordinates.length; index += 2) {
      xs.push(coordinates[index]);
      ys.push(coordinates[index + 1]);
    }
    if (xs.length) {
      return {
        minX: Math.min(...xs) - margin,
        maxX: Math.max(...xs) + margin,
        minY: Math.min(...ys) - margin,
        maxY: Math.max(...ys) + margin,
      };
    }
  }
  const width = Number(pad.pad?.[1] || 0);
  const height = Number(pad.pad?.[2] || width);
  const radians = ((Number(pad.rotation) || 0) * Math.PI) / 180;
  const halfX = Math.abs(Math.cos(radians)) * width * 0.5 + Math.abs(Math.sin(radians)) * height * 0.5;
  const halfY = Math.abs(Math.sin(radians)) * width * 0.5 + Math.abs(Math.cos(radians)) * height * 0.5;
  return {
    minX: pad.x - halfX - margin,
    maxX: pad.x + halfX + margin,
    minY: pad.y - halfY - margin,
    maxY: pad.y + halfY + margin,
  };
}

function regionObstacles(layer, margin) {
  const result = [];
  for (const region of geometry.regions) {
    if (region.layer !== 12 && region.layer !== layer) continue;
    if (!(region.ruleType || []).some((rule) => [5, 6, 7, 8].includes(rule))) continue;
    const shape = region.complexPolygon?.polygon || [];
    if (shape[0] === "R") {
      const x0 = shape[1];
      const y0 = shape[2];
      const x1 = x0 + shape[3];
      const y1 = y0 - shape[4];
      result.push({
        minX: Math.min(x0, x1) - margin,
        maxX: Math.max(x0, x1) + margin,
        minY: Math.min(y0, y1) - margin,
        maxY: Math.max(y0, y1) + margin,
      });
    } else if (shape[0] === "CIRCLE") {
      const radius = shape[3] * 0.5 + margin;
      result.push({
        minX: shape[1] - radius,
        maxX: shape[1] + radius,
        minY: shape[2] - radius,
        maxY: shape[2] + radius,
      });
    }
  }
  return result;
}

function compact(points) {
  const unique = points.filter(
    (point, index) =>
      index === 0 ||
      Math.abs(point.x - points[index - 1].x) > 1e-6 ||
      Math.abs(point.y - points[index - 1].y) > 1e-6,
  );
  const output = [];
  for (const point of unique) {
    while (output.length >= 2) {
      const a = output[output.length - 2];
      const b = output[output.length - 1];
      const cross = (b.x - a.x) * (point.y - b.y) - (b.y - a.y) * (point.x - b.x);
      if (Math.abs(cross) > 1e-6) break;
      output.pop();
    }
    output.push(point);
  }
  return output;
}

function canonicalPaths(a, b) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const absX = Math.abs(dx);
  const absY = Math.abs(dy);
  const signX = Math.sign(dx) || 1;
  const signY = Math.sign(dy) || 1;
  let first;
  let second;
  if (absX >= absY) {
    first = { x: b.x - signX * absY, y: a.y };
    second = { x: a.x + signX * absY, y: b.y };
  } else {
    first = { x: a.x, y: b.y - signY * absX };
    second = { x: b.x, y: a.y + signY * absX };
  }
  return [compact([a, first, b]), compact([a, second, b])];
}

function directionIndex(a, b) {
  const dx = Math.sign(b.x - a.x);
  const dy = Math.sign(b.y - a.y);
  return directions.findIndex(([x, y]) => x === dx && y === dy);
}

function turnDelta(a, b) {
  if (a < 0 || b < 0) return 0;
  const delta = Math.abs(a - b);
  return Math.min(delta, 8 - delta);
}

function assertAngles(path) {
  for (let index = 1; index < path.length; index += 1) {
    const dx = Math.abs(path[index].x - path[index - 1].x);
    const dy = Math.abs(path[index].y - path[index - 1].y);
    if (dx > 1e-6 && dy > 1e-6 && Math.abs(dx - dy) > 1e-6) {
      throw new Error(`Non-45 segment: ${JSON.stringify([path[index - 1], path[index]])}`);
    }
  }
}

class MinHeap {
  constructor() {
    this.items = [];
  }

  push(item) {
    this.items.push(item);
    let index = this.items.length - 1;
    while (index > 0) {
      const parent = (index - 1) >> 1;
      if (this.items[parent].priority <= item.priority) break;
      this.items[index] = this.items[parent];
      index = parent;
    }
    this.items[index] = item;
  }

  pop() {
    if (!this.items.length) return undefined;
    const root = this.items[0];
    const last = this.items.pop();
    if (this.items.length) {
      let index = 0;
      while (true) {
        let child = index * 2 + 1;
        if (child >= this.items.length) break;
        if (child + 1 < this.items.length && this.items[child + 1].priority < this.items[child].priority) child += 1;
        if (this.items[child].priority >= last.priority) break;
        this.items[index] = this.items[child];
        index = child;
      }
      this.items[index] = last;
    }
    return root;
  }
}

function planRoute(net, width, layer, start, end, margin) {
  const clearance = 6 + width * 0.5;
  const minX = Math.max(board.minX + clearance, Math.min(start.x, end.x) - margin);
  const maxX = Math.min(board.maxX - clearance, Math.max(start.x, end.x) + margin);
  const minY = Math.max(board.minY + clearance, Math.min(start.y, end.y) - margin);
  const maxY = Math.min(board.maxY - clearance, Math.max(start.y, end.y) + margin);
  const inSearchBounds = (box) => box.maxX >= minX && box.minX <= maxX && box.maxY >= minY && box.minY <= maxY;
  const padObstacles = geometry.pads
    .filter((pad) => pad.net !== net && (pad.layer === layer || pad.layer === 12))
    .map((pad) => padBox(pad, clearance))
    .filter(inSearchBounds);
  const lineObstacles = geometry.lines
    .filter((line) => line.net !== net && line.layer === layer)
    .map((line) => ({
      a: { x: line.startX, y: line.startY },
      b: { x: line.endX, y: line.endY },
      clearance: 6 + width * 0.5 + line.lineWidth * 0.5,
    }));
  const viaObstacles = geometry.vias
    .filter((via) => via.net !== net)
    .map((via) => ({ x: via.x, y: via.y, radius: via.diameter * 0.5 + clearance }));
  const keepoutObstacles = regionObstacles(layer, clearance).filter(inSearchBounds);

  function edgeAllowed(a, b) {
    if (b.x < minX || b.x > maxX || b.y < minY || b.y > maxY) return false;
    for (const box of padObstacles) if (segmentIntersectsBox(a, b, box)) return false;
    for (const box of keepoutObstacles) if (segmentIntersectsBox(a, b, box)) return false;
    for (const line of lineObstacles) if (segmentDistance(a, b, line.a, line.b) < line.clearance - 1e-6) return false;
    for (const via of viaObstacles) if (pointSegmentDistance(via, a, b) < via.radius - 1e-6) return false;
    return true;
  }

  function finalConnection(node, previousDirection) {
    if (Math.hypot(node.x - end.x, node.y - end.y) > 90) return null;
    for (const path of canonicalPaths(node, end)) {
      let valid = true;
      let direction = previousDirection;
      for (let index = 1; index < path.length; index += 1) {
        const nextDirection = directionIndex(path[index - 1], path[index]);
        if (turnDelta(direction, nextDirection) > 2 || !edgeAllowed(path[index - 1], path[index])) {
          valid = false;
          break;
        }
        direction = nextDirection;
      }
      if (valid) return path.slice(1);
    }
    return null;
  }

  const heap = new MinHeap();
  const scores = new Map();
  const parents = new Map();
  const nodes = new Map();
  const startState = { i: 0, j: 0, direction: -1, x: start.x, y: start.y };
  const startKey = "0,0,-1";
  scores.set(startKey, 0);
  nodes.set(startKey, startState);
  heap.push({ key: startKey, score: 0, priority: Math.hypot(end.x - start.x, end.y - start.y) });
  let goal;
  let iterations = 0;

  while (heap.items.length && iterations < 400000) {
    iterations += 1;
    const currentItem = heap.pop();
    if (currentItem.score !== scores.get(currentItem.key)) continue;
    const current = nodes.get(currentItem.key);
    const tail = finalConnection(current, current.direction);
    if (tail) {
      goal = { key: currentItem.key, tail };
      break;
    }
    for (let direction = 0; direction < directions.length; direction += 1) {
      if (turnDelta(current.direction, direction) > 2) continue;
      const [di, dj] = directions[direction];
      const next = {
        i: current.i + di,
        j: current.j + dj,
        direction,
        x: start.x + (current.i + di) * grid,
        y: start.y + (current.j + dj) * grid,
      };
      if (!edgeAllowed(current, next)) continue;
      const key = `${next.i},${next.j},${direction}`;
      const turnCost = current.direction < 0 || current.direction === direction ? 0 : 18 * turnDelta(current.direction, direction);
      const nextScore = currentItem.score + Math.hypot(di, dj) * grid + turnCost;
      if (nextScore >= (scores.get(key) ?? Infinity)) continue;
      scores.set(key, nextScore);
      parents.set(key, currentItem.key);
      nodes.set(key, next);
      heap.push({ key, score: nextScore, priority: nextScore + Math.hypot(end.x - next.x, end.y - next.y) });
    }
  }

  if (!goal) return { path: null, iterations };
  const reverse = [];
  let key = goal.key;
  while (key) {
    const node = nodes.get(key);
    reverse.push({ x: node.x, y: node.y });
    key = parents.get(key);
  }
  reverse.reverse();
  const path = compact([...reverse, ...goal.tail]);
  assertAngles(path);
  return { path, iterations };
}

async function createRoute(net, width, layer, path) {
  const definitions = [];
  for (let index = 1; index < path.length; index += 1) {
    definitions.push({
      sx: path[index - 1].x,
      sy: path[index - 1].y,
      ex: path[index].x,
      ey: path[index].y,
    });
  }
  return execute(
    `const defs=${JSON.stringify(definitions)}; const ids=[]; for(const d of defs){const line=await eda.pcb_PrimitiveLine.create(${JSON.stringify(net)},${layer},d.sx,d.sy,d.ex,d.ey,${width},false); if(!line)throw new Error("line create failed"); ids.push(line.primitiveId);} return ids;`,
  );
}

async function validate(net) {
  return execute(
    `const drc=await eda.pcb_Drc.check(true,false,true); const bad=(drc||[]).filter(x=>x.name!=="Connection Error"&&(x.count||0)>0).map(x=>({name:x.name,count:x.count,groups:(x.list||[]).map(g=>({name:g.name,count:g.count}))})); const c=(drc||[]).find(x=>x.name==="Connection Error"); const connection=c?.list?.find(x=>x.name===${JSON.stringify(net)})?.count||0; return {bad,connection,categories:(drc||[]).map(x=>({name:x.name,count:x.count}))};`,
  );
}

async function deleteLines(ids) {
  if (!ids.length) return;
  await execute(`return await eda.pcb_PrimitiveLine.delete(${JSON.stringify(ids)});`);
}

for (const target of targets) {
  const existing = geometry.lines.filter((line) => line.net === target.net);
  if (existing.length && !target.start) {
    console.log(JSON.stringify({ net: target.net, status: "skip", reason: "already routed", lines: existing.length }));
    continue;
  }
  let layer = target.layer;
  let start = target.start;
  let end = target.end;
  if (!start) {
    const pads = geometry.pads.filter((pad) => pad.net === target.net);
    if (pads.length !== 2 || pads.some((pad) => pad.layer !== pads[0].layer)) {
      console.log(JSON.stringify({ net: target.net, status: "skip", reason: "not a same-layer two-pad net", pads: pads.length }));
      continue;
    }
    layer = pads[0].layer;
    [start, end] = pads;
  }
  let accepted = false;
  for (const margin of [220, 420, 700, 1000, 1400]) {
    const started = Date.now();
    const plan = planRoute(target.net, target.width, layer, start, end, margin);
    if (!plan.path) {
      console.log(JSON.stringify({ net: target.net, status: "no-path", margin, iterations: plan.iterations, ms: Date.now() - started }));
      continue;
    }
    const ids = await createRoute(target.net, target.width, layer, plan.path);
    const validation = await validate(target.net);
    if (!validation.bad.length && validation.connection === 0) {
      for (let index = 1; index < plan.path.length; index += 1) {
        geometry.lines.push({
          primitiveId: ids[index - 1],
          net: target.net,
          layer,
          startX: plan.path[index - 1].x,
          startY: plan.path[index - 1].y,
          endX: plan.path[index].x,
          endY: plan.path[index].y,
          lineWidth: target.width,
        });
      }
      console.log(
        JSON.stringify({
          net: target.net,
          status: "accepted",
          width: target.width,
          margin,
          iterations: plan.iterations,
          ms: Date.now() - started,
          path: plan.path,
          ids,
          drc: validation.categories,
        }),
      );
      accepted = true;
      break;
    }
    await deleteLines(ids);
    console.log(
      JSON.stringify({
        net: target.net,
        status: "rejected",
        margin,
        iterations: plan.iterations,
        ms: Date.now() - started,
        bad: validation.bad,
        connection: validation.connection,
      }),
    );
  }
  if (!accepted) console.log(JSON.stringify({ net: target.net, status: "unrouted" }));
}

const finalDrc = await execute(
  "const drc=await eda.pcb_Drc.check(true,false,true); return (drc||[]).map(x=>({name:x.name,count:x.count}));",
);
const saved = await execute("return await eda.pcb_Document.save();");
console.log(JSON.stringify({ status: "complete", windowId, saved, drc: finalDrc }));
