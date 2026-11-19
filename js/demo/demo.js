// The demo page: renders a fixture or a local file with the built package,
// so that the result can be compared with Word. Local files are read in the
// browser with FileReader; nothing is uploaded or stored.
import { load, renderHtml, logicalTables, RtfError } from '../dist/src/index.js';

const elements = {
  document: document.getElementById('document'),
  fixtures: document.getElementById('fixtures'),
  local: document.getElementById('local'),
  files: document.getElementById('files'),
  pageNumbers: document.getElementById('page-numbers'),
  pictures: document.getElementById('pictures'),
  zoom: document.getElementById('zoom'),
  showTables: document.getElementById('show-tables'),
  status: document.getElementById('status'),
  warnings: document.getElementById('warnings'),
  warningsSummary: document.getElementById('warnings-summary'),
  warningsList: document.getElementById('warnings-list'),
  pages: document.getElementById('pages'),
  tables: document.getElementById('tables'),
};

// The bytes of the files chosen in the file input, by option value.
const localFiles = new Map();
let localCount = 0;

let rtf;
// The document on screen and the time its parse took, in milliseconds.
let current;
// Counts the documents opened, so that a slow read cannot replace a newer one.
let opened = 0;

function showStatus(text, isError = false) {
  elements.status.textContent = text;
  elements.status.classList.toggle('error', isError);
}

function errorText(error) {
  if (error instanceof RtfError) return `error ${error.code}: ${error.message}`;
  return error instanceof Error ? error.message : String(error);
}

function addOption(group, value, label) {
  const option = document.createElement('option');
  option.value = value;
  option.textContent = label;
  group.append(option);
  return option;
}

async function loadFixtureList() {
  const response = await fetch('/api/fixtures');
  if (!response.ok) throw new Error(`the fixture list answered ${response.status}`);
  const names = await response.json();
  for (const name of names) addOption(elements.fixtures, `fixture:${name}`, name);
}

function readFile(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = () => reject(reader.error ?? new Error(`cannot read ${file.name}`));
    reader.readAsArrayBuffer(file);
  });
}

async function bytesOf(value) {
  if (localFiles.has(value)) return localFiles.get(value);
  const name = value.slice('fixture:'.length);
  const response = await fetch(`/test/fixtures/${encodeURIComponent(name)}.rtf`);
  if (!response.ok) throw new Error(`${name}.rtf answered ${response.status}`);
  return response.arrayBuffer();
}

function renderOptions() {
  return {
    pageNumbers: elements.pageNumbers.checked ? 'sections' : 'none',
    pictures: elements.pictures.checked,
    pxPerTwip: Number(elements.zoom.value) / 100 / 15,
  };
}

function clearOutput() {
  elements.pages.replaceChildren();
  elements.tables.replaceChildren();
  elements.tables.hidden = true;
  showWarnings([]);
}

function showWarnings(warnings) {
  elements.warningsSummary.textContent = `Warnings (${warnings.length})`;
  const items = warnings.map((warning) => {
    const item = document.createElement('li');
    item.textContent = `${warning.code} at byte ${warning.offset}: ${warning.message}`;
    return item;
  });
  elements.warningsList.replaceChildren(...items);
  elements.warnings.hidden = warnings.length === 0;
}

function renderTable(table) {
  const element = document.createElement('table');
  const caption = document.createElement('caption');
  const sections =
    table.firstSection === table.lastSection
      ? `section ${table.firstSection}`
      : `sections ${table.firstSection} to ${table.lastSection}`;
  caption.textContent = `Table ${table.id}: ${sections}, ${table.columnNames.length} columns`;
  element.append(caption);

  const head = document.createElement('thead');
  const headRow = document.createElement('tr');
  for (const name of table.columnNames) {
    const cell = document.createElement('th');
    cell.textContent = name;
    headRow.append(cell);
  }
  head.append(headRow);
  element.append(head);

  const body = document.createElement('tbody');
  for (const row of table.rows) {
    const line = document.createElement('tr');
    if (row.spacer) line.className = 'spacer';
    for (const cell of row.cells) {
      const td = document.createElement('td');
      td.textContent = cell.text;
      if (cell.span > 1) td.colSpan = cell.span;
      line.append(td);
    }
    body.append(line);
  }
  element.append(body);
  return element;
}

function renderTables(doc) {
  if (!elements.showTables.checked) {
    elements.tables.replaceChildren();
    elements.tables.hidden = true;
    return;
  }
  const tables = logicalTables(doc);
  const heading = document.createElement('h2');
  heading.textContent = `Logical tables (${tables.length})`;
  elements.tables.replaceChildren(heading, ...tables.map(renderTable));
  elements.tables.hidden = false;
}

function renderDocument() {
  if (current === undefined) return;
  const { doc, parseMs } = current;
  const start = performance.now();
  const html = renderHtml(doc, renderOptions());
  const renderMs = performance.now() - start;
  // The only innerHTML of the page: renderHtml escapes the document's text.
  elements.pages.innerHTML = html;
  const pages = elements.pages.querySelectorAll('.rtf-page').length;
  showStatus(
    `core ${rtf.version} | parse ${parseMs.toFixed(1)} ms | render ${renderMs.toFixed(1)} ms` +
      ` | ${pages} pages | ${doc.warnings.length} warnings`,
  );
  showWarnings(doc.warnings);
  renderTables(doc);
}

async function openDocument(value) {
  const ticket = ++opened;
  current = undefined;
  clearOutput();
  showStatus('Reading the document.');
  try {
    const bytes = await bytesOf(value);
    if (ticket !== opened) return;
    const start = performance.now();
    const doc = rtf.parse(bytes);
    current = { doc, parseMs: performance.now() - start };
    renderDocument();
  } catch (error) {
    if (ticket !== opened) return;
    current = undefined;
    clearOutput();
    showStatus(errorText(error), true);
  }
}

async function addLocalFiles(files) {
  let first;
  for (const file of files) {
    try {
      const bytes = await readFile(file);
      const value = `local:${localCount++}`;
      localFiles.set(value, bytes);
      addOption(elements.local, value, file.name);
      first ??= value;
    } catch (error) {
      showStatus(errorText(error), true);
    }
  }
  elements.local.hidden = localFiles.size === 0;
  if (first !== undefined) {
    elements.document.value = first;
    await openDocument(first);
  }
}

function renderOrReport() {
  try {
    renderDocument();
  } catch (error) {
    showStatus(errorText(error), true);
  }
}

async function start() {
  try {
    rtf = await load();
    await loadFixtureList();
  } catch (error) {
    showStatus(errorText(error), true);
    return;
  }
  elements.document.addEventListener('change', () => openDocument(elements.document.value));
  elements.files.addEventListener('change', () => {
    addLocalFiles([...elements.files.files]);
    elements.files.value = '';
  });
  for (const control of [elements.pageNumbers, elements.pictures, elements.zoom, elements.showTables]) {
    control.addEventListener('change', renderOrReport);
  }
  showStatus(`core ${rtf.version} | choose a document`);
  if (elements.document.value !== '') await openDocument(elements.document.value);
}

start();
