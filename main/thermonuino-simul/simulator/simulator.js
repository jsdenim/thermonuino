const slotsPerWeek = 7 * 24 * 4;
const dayNames = ["Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"];

const moduleStatus = document.querySelector("#module-status");
const slotLabel = document.querySelector("#slot-label");
const absoluteLabel = document.querySelector("#absolute-label");
const decisionLabel = document.querySelector("#decision-label");
const targetLabel = document.querySelector("#target-label");
const powerLabel = document.querySelector("#power-label");
const modeLabel = document.querySelector("#mode-label");
const slotRange = document.querySelector("#slot-range");
const playButton = document.querySelector("#play-button");
const stepButton = document.querySelector("#step-button");
const resetButton = document.querySelector("#reset-button");
const rerunButton = document.querySelector("#rerun-button");
const loopToggle = document.querySelector("#loop-toggle");
const speedSelect = document.querySelector("#speed-select");
const measuredTempInput = document.querySelector("#measured-temp");
const baseTempInput = document.querySelector("#base-temp");
const userVariationInput = document.querySelector("#user-variation");
const variationMinusButton = document.querySelector("#variation-minus");
const variationPlusButton = document.querySelector("#variation-plus");
const presenceToggle = document.querySelector("#presence-toggle");
const weekChart = document.querySelector("#week-chart");
const serialLog = document.querySelector("#serial-log");
const clearLogButton = document.querySelector("#clear-log-button");

let wasm = {
  evaluateSlot: null,
  setup: null,
  reset: null,
};
let timer = null;
let absoluteSlot = 0;
let weekResults = Array(slotsPerWeek).fill(null);
let presenceDetected = true;
const chartContext = weekChart.getContext("2d");

function readNumber(input) {
  return Number.parseFloat(input.value);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function setPresence(value) {
  presenceDetected = value;
  presenceToggle.setAttribute("aria-pressed", String(value));
  presenceToggle.textContent = value ? "Detectee" : "Absente";
  presenceToggle.classList.toggle("is-off", !value);
}

function stepVariation(delta) {
  const min = Number.parseFloat(userVariationInput.min);
  const max = Number.parseFloat(userVariationInput.max);
  const nextValue = clamp(readNumber(userVariationInput) + delta, min, max);
  userVariationInput.value = nextValue.toFixed(1);
  executeSlot(true);
}

function formatSlot(slotOfWeek) {
  const day = Math.floor(slotOfWeek / 96);
  const slotOfDay = slotOfWeek % 96;
  const minutes = slotOfDay * 15;
  const hour = Math.floor(minutes / 60);
  const minute = minutes % 60;

  return `${dayNames[day]} ${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}`;
}

function appendLog(entry, replayOnly) {
  const line = [
    replayOnly ? "REPLAY" : "RUN",
    `#${entry.absoluteSlot}`,
    formatSlot(entry.slotOfWeek),
    entry.mode,
    `target=${entry.learnedTarget.toFixed(1)}`,
    `variation=${entry.userVariation.toFixed(1)}`,
    `presence=${entry.presenceDetected ? "yes" : "no"}`,
    `measured=${entry.measured.toFixed(1)}`,
    `power=${entry.power}`,
  ].join(" | ");

  serialLog.textContent = `${line}\n${serialLog.textContent}`.slice(0, 12000);
}

function render(entry, replayOnly) {
  slotRange.value = String(entry.slotOfWeek);
  slotLabel.textContent = formatSlot(entry.slotOfWeek);
  absoluteLabel.textContent = `Execution ${entry.absoluteSlot}`;
  decisionLabel.textContent = entry.heating ? "Chauffe" : "Arret";
  targetLabel.textContent = `Consigne ${entry.learnedTarget.toFixed(1)} C`;
  powerLabel.textContent = `${entry.power} %`;
  modeLabel.textContent = `Mode ${entry.mode}`;
  weekResults[entry.slotOfWeek] = entry;
  drawChart();
  appendLog(entry, replayOnly);
}

function chartX(slot, bounds) {
  return bounds.left + (slot / (slotsPerWeek - 1)) * bounds.width;
}

function chartY(temp, minTemp, maxTemp, bounds) {
  return bounds.top + ((maxTemp - temp) / (maxTemp - minTemp)) * bounds.height;
}

function drawChart() {
  const rect = weekChart.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  const cssWidth = Math.max(320, Math.floor(rect.width));
  const cssHeight = 300;

  if (weekChart.width !== cssWidth * ratio || weekChart.height !== cssHeight * ratio) {
    weekChart.width = cssWidth * ratio;
    weekChart.height = cssHeight * ratio;
  }

  chartContext.setTransform(ratio, 0, 0, ratio, 0, 0);
  chartContext.clearRect(0, 0, cssWidth, cssHeight);

  const bounds = {
    left: 46,
    top: 18,
    width: cssWidth - 66,
    height: cssHeight - 54,
  };
  const values = weekResults.filter(Boolean);
  const baseTemp = readNumber(baseTempInput);
  const rawTemps = values.flatMap((entry) => [
    entry.learnedTarget,
    entry.baseTemp + entry.userVariation,
  ]);
  const minTemp = Math.floor(Math.min(baseTemp - 3, ...rawTemps) - 1);
  const maxTemp = Math.ceil(Math.max(baseTemp + 4, ...rawTemps) + 1);

  chartContext.fillStyle = "#ffffff";
  chartContext.fillRect(0, 0, cssWidth, cssHeight);
  chartContext.strokeStyle = "#d8dee7";
  chartContext.lineWidth = 1;

  for (let day = 0; day <= 7; day += 1) {
    const x = bounds.left + (day / 7) * bounds.width;
    chartContext.beginPath();
    chartContext.moveTo(x, bounds.top);
    chartContext.lineTo(x, bounds.top + bounds.height);
    chartContext.stroke();
  }

  for (let temp = minTemp; temp <= maxTemp; temp += 1) {
    const y = chartY(temp, minTemp, maxTemp, bounds);
    chartContext.beginPath();
    chartContext.moveTo(bounds.left, y);
    chartContext.lineTo(bounds.left + bounds.width, y);
    chartContext.stroke();

    if (temp % 2 === 0) {
      chartContext.fillStyle = "#607083";
      chartContext.fillText(`${temp} C`, 8, y + 4);
    }
  }

  chartContext.strokeStyle = "#1b6fd8";
  chartContext.lineWidth = 2;
  chartContext.beginPath();
  let started = false;
  weekResults.forEach((entry, slot) => {
    if (!entry) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = chartY(entry.learnedTarget, minTemp, maxTemp, bounds);
    if (!started) {
      chartContext.moveTo(x, y);
      started = true;
      return;
    }
    chartContext.lineTo(x, y);
  });
  chartContext.stroke();

  weekResults.forEach((entry, slot) => {
    if (!entry || Math.abs(entry.userVariation) < 0.001) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = chartY(entry.baseTemp + entry.userVariation, minTemp, maxTemp, bounds);
    chartContext.beginPath();
    chartContext.arc(x, y, 3.5, 0, Math.PI * 2);
    chartContext.fillStyle = entry.userVariation > 0 ? "#d94b35" : "#2b8c5f";
    chartContext.fill();
  });

  chartContext.fillStyle = "#607083";
  chartContext.textAlign = "center";
  dayNames.forEach((day, index) => {
    const x = bounds.left + ((index + 0.5) / 7) * bounds.width;
    chartContext.fillText(day, x, cssHeight - 16);
  });
  chartContext.textAlign = "start";
}

function executeSlot(replayOnly = false) {
  if (!wasm.evaluateSlot) {
    return;
  }

  const json = wasm.evaluateSlot(
    absoluteSlot,
    readNumber(measuredTempInput),
    readNumber(userVariationInput),
    presenceDetected ? 1 : 0,
    replayOnly ? 1 : 0,
  );
  const entry = JSON.parse(json);
  render(entry, replayOnly);
}

function stopPlayback() {
  if (timer) {
    window.clearInterval(timer);
    timer = null;
  }
  playButton.textContent = "Lecture";
}

function schedulePlayback() {
  stopPlayback();
  timer = window.setInterval(() => {
    stepForward();
  }, Number.parseInt(speedSelect.value, 10));
  playButton.textContent = "Pause";
}

function stepForward() {
  const nextSlotOfWeek = (absoluteSlot + 1) % slotsPerWeek;

  if (!loopToggle.checked && nextSlotOfWeek === 0 && absoluteSlot > 0) {
    stopPlayback();
    return;
  }

  absoluteSlot += 1;
  executeSlot(false);
}

function resetSimulation() {
  stopPlayback();
  absoluteSlot = 0;
  weekResults = Array(slotsPerWeek).fill(null);
  if (wasm.setup) {
    wasm.setup(readNumber(baseTempInput));
  } else if (wasm.reset) {
    wasm.reset();
  }
  serialLog.textContent = "";
  executeSlot(false);
}

createGreetingsModule().then((module) => {
  wasm.evaluateSlot = module.cwrap("evaluateThermostatSlot", "string", [
    "number",
    "number",
    "number",
    "number",
    "number",
  ]);
  wasm.setup = module.cwrap("setupThermostat", null, ["number"]);
  wasm.reset = module.cwrap("resetThermostat", null, []);
  moduleStatus.textContent = "WASM pret";
  resetSimulation();
});

playButton.addEventListener("click", () => {
  if (timer) {
    stopPlayback();
    return;
  }
  schedulePlayback();
});

stepButton.addEventListener("click", () => {
  stopPlayback();
  stepForward();
});

resetButton.addEventListener("click", resetSimulation);
rerunButton.addEventListener("click", () => executeSlot(true));
clearLogButton.addEventListener("click", () => {
  serialLog.textContent = "";
});

slotRange.addEventListener("input", () => {
  stopPlayback();
  const weekBase = Math.floor(absoluteSlot / slotsPerWeek) * slotsPerWeek;
  absoluteSlot = weekBase + Number.parseInt(slotRange.value, 10);
  executeSlot(true);
});

speedSelect.addEventListener("change", () => {
  if (timer) {
    schedulePlayback();
  }
});

[measuredTempInput, userVariationInput].forEach((input) => {
  input.addEventListener("change", () => executeSlot(true));
});

variationMinusButton.addEventListener("click", () => stepVariation(-0.5));
variationPlusButton.addEventListener("click", () => stepVariation(0.5));
presenceToggle.addEventListener("click", () => {
  setPresence(!presenceDetected);
  executeSlot(true);
});

baseTempInput.addEventListener("change", resetSimulation);
window.addEventListener("resize", drawChart);
