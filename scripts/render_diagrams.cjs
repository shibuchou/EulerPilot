// Offline rendering and geometry QA for diagrams created by build_diagrams.py.
// Uses an existing Playwright installation and the local Chrome executable.
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

async function main() {
  const root = path.resolve(__dirname, '..');
  const figures = path.join(root, 'docs', 'assets', 'figures');
  const output = path.join(root, 'build', 'diagram-qa');
  fs.mkdirSync(path.join(output, 'png'), { recursive: true });
  const models = JSON.parse(fs.readFileSync(path.join(figures, 'diagrams.json'), 'utf8'));
  const browser = await chromium.launch({ executablePath: process.env.DIAGRAM_CHROME || 'C:/Program Files/Google/Chrome/Application/chrome.exe', headless: true, args: ['--disable-gpu'] });
  const results = [];
  try {
    for (const model of models) {
      const page = await browser.newPage({ viewport: { width: model.width, height: model.height }, deviceScaleFactor: 2 });
      const markup = fs.readFileSync(path.join(figures, model.slug + '.svg'), 'utf8');
      await page.setContent('<!doctype html><html><head><meta charset="UTF-8"><style>html,body{margin:0;padding:0;background:white}svg{display:block}</style></head><body>' + markup + '</body></html>');
      await page.evaluate(() => document.fonts.ready);
      const issues = await page.evaluate((model) => {
        const errors = [];
        const svg = document.querySelector('svg');
        const bounds = svg.viewBox.baseVal;
        for (const text of document.querySelectorAll('svg text')) {
          const box = text.getBBox();
          if (box.x < 0 || box.y < 0 || box.x + box.width > bounds.width + 1 || box.y + box.height > bounds.height + 1) {
            errors.push({ type: 'text-outside-canvas', text: text.textContent, box: { x: box.x, y: box.y, width: box.width, height: box.height } });
          }
          const node = text.closest('[data-node]');
          if (node) {
            const [x, y, w, h] = node.dataset.box.split(',').map(Number);
            if (box.x < x + 10 || box.y < y + 8 || box.x + box.width > x + w - 10 || box.y + box.height > y + h - 8) {
              errors.push({ type: 'text-outside-node-padding', node: node.dataset.node, text: text.textContent, rightOverflow: box.x + box.width - (x + w - 10), bottomOverflow: box.y + box.height - (y + h - 8) });
            }
          }
        }
        for (const shape of document.querySelectorAll('path[data-edge]')) {
          const edge = model.edges[Number(shape.dataset.edge)];
          const hits = new Set();
          const length = shape.getTotalLength();
          for (let pos = 0; pos < length; pos += 3) {
            const point = shape.getPointAtLength(pos);
            for (const node of model.nodes) {
              if (node.id === edge.source || node.id === edge.target) continue;
              if (point.x > node.x + 3 && point.x < node.x + node.w - 3 && point.y > node.y + 3 && point.y < node.y + node.h - 3) hits.add(node.id);
            }
          }
          if (hits.size) errors.push({ type: 'edge-crosses-unrelated-node', edge: shape.dataset.edge, source: edge.source, target: edge.target, hits: [...hits] });
        }
        return errors;
      }, model);
      console.log(model.slug + ': ' + issues.length + ' geometry issues');
      results.push({ slug: model.slug, issues });
      fs.writeFileSync(path.join(output, 'geometry-qa.json'), JSON.stringify(results, null, 2));
      await page.screenshot({ path: path.join(output, 'png', model.slug + '.png'), clip: { x: 0, y: 0, width: model.width, height: model.height }, animations: 'disabled', timeout: 15000 });
      await page.close();
    }
  } finally { await browser.close(); }
  fs.writeFileSync(path.join(output, 'geometry-qa.json'), JSON.stringify(results, null, 2));
  const bad = results.filter(r => r.issues.length);
  console.log(JSON.stringify({ figures: models.length, diagramsWithIssues: bad.length, issues: bad }, null, 2));
  if (bad.length) process.exitCode = 1;
}
main().catch(error => { console.error(error); process.exitCode = 1; });
