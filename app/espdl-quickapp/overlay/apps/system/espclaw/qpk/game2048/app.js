'use strict';
const viewport = ui.getSize();
const W = viewport.width, H = viewport.height;
const controlsY = H - 48;
const tileGap = 8;
const boardSize = Math.min(W - 40, controlsY - 54);
const tileSize = Math.floor((boardSize - 16 - tileGap * 3) / 4);
const actualBoard = tileSize * 4 + tileGap * 3 + 16;
const boardX = Math.floor((W - actualBoard) / 2);
const boardY = 46;
const scoreLabel = ui.text('分数 0', 20, 10, 20, 0x776e65);
const bestLabel = ui.text('最高 0', 270, 10, 20, 0x776e65);
ui.setSize(scoreLabel, 230, 32);
ui.setSize(bestLabel, 360, 32);
const statusLabel = ui.text('准备开始', W - 170, 12, 16, 0x776e65);
ui.background(0xfaf8ef);
const boardPanel = ui.panel(boardX, boardY, actualBoard, actualBoard, 0xbbada0, 10, 255);
const tilePanels = [], tileLabels = [];
for (let i = 0; i < 16; i++) {
  const x = boardX + 8 + (i % 4) * (tileSize + tileGap);
  const y = boardY + 8 + Math.floor(i / 4) * (tileSize + tileGap);
  tilePanels.push(ui.panel(x, y, tileSize, tileSize, 0xcdc1b4, 7, 255));
  tileLabels.push(ui.number('', x, y, tileSize, tileSize, 0x776e65));
}
const board = [];
let score = 0;
let best = 0;
let gameOver = false;
let won = false;
try { const value = Number(system.storage.get('best') || 0); if (isFinite(value) && value > 0) best = Math.floor(value); } catch (e) {}
function setText(handle, value) { ui.setText(handle, String(value)); }
function tileColor(value) {
  if (value === 2) return 0xeee4da;
  if (value === 4) return 0xede0c8;
  if (value === 8) return 0xf2b179;
  if (value === 16) return 0xf59563;
  if (value === 32) return 0xf67c5f;
  if (value === 64) return 0xf65e3b;
  if (value === 128) return 0xedcf72;
  if (value === 256) return 0xedcc61;
  if (value === 512) return 0xedc850;
  if (value === 1024) return 0xedc53f;
  if (value >= 2048) return 0xedc22e;
  return 0xcdc1b4;
}
function textColor(value) { return value <= 4 ? 0x776e65 : 0xffffff; }
function addTile() {
  const empty = [];
  for (let i = 0; i < 16; i++) if (board[i] === 0) empty.push(i);
  if (!empty.length) return;
  const index = empty[Math.floor(Math.random() * empty.length)];
  board[index] = Math.random() < 0.9 ? 2 : 4;
}
function reset() {
  board.length = 0;
  for (let i = 0; i < 16; i++) board.push(0);
  score = 0; gameOver = false; won = false; addTile(); addTile(); draw();
}
function slide(line) {
  const values = [];
  const result = [];
  for (let i = 0; i < 4; i++) if (line[i]) values.push(line[i]);
  for (let i = 0; i < values.length; i++) {
    if (i + 1 < values.length && values[i] === values[i + 1]) {
      const merged = values[i] * 2; result.push(merged); score += merged;
      if (merged === 2048) won = true; i++;
    } else result.push(values[i]);
  }
  while (result.length < 4) result.push(0);
  for (let i = 0; i < 4; i++) if (result[i] !== line[i]) return {line:result, changed:true};
  return {line:result, changed:false};
}
function move(direction) {
  if (gameOver) return;
  let changed = false;
  for (let n = 0; n < 4; n++) {
    const line = [];
    for (let k = 0; k < 4; k++) {
      const source = direction === 'right' || direction === 'down' ? 3 - k : k;
      const p = direction === 'left' || direction === 'right' ? n * 4 + source : source * 4 + n;
      line[k] = board[p];
    }
    const result = slide(line);
    if (result.changed) changed = true;
    for (let k = 0; k < 4; k++) {
      const target = direction === 'right' || direction === 'down' ? 3 - k : k;
      const p = direction === 'left' || direction === 'right' ? n * 4 + target : target * 4 + n;
      board[p] = result.line[k];
    }
  }
  if (!changed) { if (!canMove()) { gameOver = true; setText(statusLabel, '游戏结束'); } return; }
  if (score > best) { best = score; try { system.storage.set('best', String(best)); } catch (e) {} }
  addTile();
  if (!canMove()) gameOver = true;
  draw();
}
function canMove() {
  for (let i = 0; i < 16; i++) {
    if (board[i] === 0) return true;
    if (i % 4 < 3 && board[i] === board[i + 1]) return true;
    if (i < 12 && board[i] === board[i + 4]) return true;
  }
  return false;
}
function draw() {
  setText(scoreLabel, '分数 ' + score); setText(bestLabel, '最高 ' + best);
  if (gameOver) setText(statusLabel, '游戏结束'); else if (won) setText(statusLabel, '达成 2048'); else setText(statusLabel, '准备开始');
  for (let i = 0; i < 16; i++) { const value = board[i]; ui.setColor(tilePanels[i], tileColor(value)); ui.setColor(tileLabels[i], textColor(value)); setText(tileLabels[i], value ? value : ''); }
}
const restart = ui.button('重新开始', 20, controlsY, 132, 42, reset, 0xf0a04b);
const left = ui.button('左', W - 292, controlsY, 62, 42, function () { move('left'); }, 0x776e65);
const up = ui.button('上', W - 224, controlsY, 62, 42, function () { move('up'); }, 0x776e65);
const down = ui.button('下', W - 156, controlsY, 62, 42, function () { move('down'); }, 0x776e65);
const right = ui.button('右', W - 88, controlsY, 62, 42, function () { move('right'); }, 0x776e65);
ui.onSwipe(function (direction) { move(direction); });
reset();
