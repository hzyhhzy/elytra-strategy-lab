(() => {
  const canvas = document.getElementById("flightCanvas");
  const ctx = canvas.getContext("2d");

  const hud = {
    noseAngle: document.getElementById("noseAngle"),
    mcPitch: document.getElementById("mcPitch"),
    vx: document.getElementById("vx"),
    vy: document.getElementById("vy"),
    speed: document.getElementById("speed"),
    altitude: document.getElementById("altitude"),
    distance: document.getElementById("distance"),
    ticks: document.getElementById("ticks"),
    durability: document.getElementById("durability"),
    pitchMarker: document.getElementById("pitchMarker"),
    statusBanner: document.getElementById("statusBanner"),
    toggleRun: document.getElementById("toggleRun"),
    resetRun: document.getElementById("resetRun"),
    autoPilot: document.getElementById("autoPilot"),
    strategyGainTwo: document.getElementById("strategyGainTwo"),
    strategyReturnHeight: document.getElementById("strategyReturnHeight"),
    strategySpeed: document.getElementById("strategySpeed"),
    strategyClimb: document.getElementById("strategyClimb"),
    strategyInfo: document.getElementById("strategyInfo"),
    formulaToggle: document.getElementById("formulaToggle"),
    formulaPanel: document.getElementById("formulaPanel"),
    closeFormula: document.getElementById("closeFormula"),
  };

  const MC = Object.freeze({
    TICKS_PER_SECOND: 20,
    DEG_TO_RAD: Math.fround(Math.PI / 180),
    RAD_TO_INDEX: Math.fround(10430.378),
    GRAVITY: -0.08,
    LIFT: 0.06,
    FALL_TO_GLIDE: -0.1,
    PITCH_UP_X: 0.04,
    PITCH_UP_Y: 3.2,
    HORIZONTAL_ALIGN: 0.1,
    HORIZONTAL_DRAG: 0.9900000095367432,
    VERTICAL_DRAG: 0.9800000190734863,
    ELYTRA_MAX_DURABILITY: 432,
  });

  const STRATEGY_MANIFEST = Object.freeze([
    Object.freeze({
      id: "fromRestGainTwo",
      label: "最低起步高度 +2",
      shortLabel: "起步+2",
      info: "217 tick · 最低起步高度 35.122 m · 终点 +2 m",
      fileName: "from-rest-gain-two/waveform.csv",
    }),
    Object.freeze({
      id: "fromRestReturnHeight",
      label: "最低起步高度 +0",
      shortLabel: "起步+0",
      info: "208 tick · 最低起步高度 32.348 m · 高度回到起点",
      fileName: "from-rest-return-height/waveform.csv",
    }),
    Object.freeze({
      id: "maxClimb",
      label: "最大爬升速度",
      shortLabel: "爬升",
      info: "254 tick · 20 m/cycle · dy +19.653",
      fileName: "fastest-climb-rate/waveform.csv",
    }),
    Object.freeze({
      id: "hardSpeed",
      label: "最快水平巡航",
      shortLabel: "巡航",
      info: "357 tick · 33.0 m/s · dy +0.000061",
      fileName: "fastest-horizontal-speed/waveform.csv",
    }),
  ]);

  const STRATEGY_BASE_PATHS = Object.freeze([
    "./strategies",
    "../strategies",
    "./elytra-strategy-lab/strategies",
  ]);

  const EMBEDDED_STRATEGY_WAVEFORMS = window.ELYTRA_STRATEGY_WAVEFORMS || null;

  const K100_AUTOPILOT_ANGLES = Object.freeze([
    12.7318080985,
    12.097411389,
    11.4940633729,
    11.5272385295,
    11.4841143117,
    10.3852848282,
    9.16250153799,
    8.94283045957,
    8.49167245519,
    7.37344130884,
    6.67075869987,
    5.93642412922,
    3.96638336542,
    1.02740045078,
    0.127913446381,
    -0.391752314574,
    -1.51206721997,
    -2.50877683676,
    -23.8986960432,
    -65.7827996615,
    -83.3890234244,
    -68.4904745327,
    -49.5061212812,
    -35.3569516504,
    -28.0957736883,
    -24.5132630761,
    -23.418038379,
    -27.2502116838,
    -25.206511011,
    -20.5692558233,
    -22.5107718521,
    -25.279989664,
    -27.3843818046,
    -27.7935556412,
    -27.2522613704,
    -26.138417743,
    -24.3334421412,
    -29.1087318556,
    -30.9651958531,
    -25.977719854,
    -27.0225362052,
    -26.4859480913,
    -22.9026910686,
    -25.1724040431,
    -28.7033393889,
    -30.7037567676,
    -29.358394136,
    -27.9800639439,
    -27.9173568925,
    -27.1684406838,
    -31.4429757532,
    -31.2771241027,
    -28.9053240086,
    -33.9561306726,
    -29.929965791,
    -28.3612363913,
    -34.8789929146,
    -25.9316213373,
    -22.7184733338,
    -33.0151392425,
    -32.8989265211,
    -32.0603741627,
    -33.8109976763,
    -32.0703424012,
    -33.3366970036,
    -34.7256817436,
    -34.5243305643,
    -32.8778186649,
    -31.7407675169,
    -34.3281409155,
    -34.6162274793,
    -35.0315571981,
    -37.0264975463,
    -33.2521865666,
    -31.1605140162,
    -35.3022856424,
    -35.0689486753,
    -33.4067359616,
    -37.7968037931,
    -37.8327497932,
    -34.9923962589,
    -38.7388542661,
    -37.2612522153,
    -33.7918888529,
    -37.6758823778,
    -37.0868496899,
    -36.6059052366,
    -38.8288006013,
    -36.027049895,
    -35.9841023557,
    -37.1740580173,
    -39.3144310657,
    -41.6796527887,
    -35.7821272172,
    -35.788778466,
    -40.7051899045,
    -38.5844892637,
    -39.0973011294,
    -37.0347787171,
    -35.0630572121,
    -41.2554517952,
    -40.2261151005,
    -38.7882119997,
    -42.3087931263,
    -36.9551002046,
    -36.0455116742,
    -44.5437822688,
    -43.8153068546,
    -39.3721222315,
    -42.3749888729,
    -42.5880518732,
    -38.7576047818,
    -38.6616032194,
    -38.5741768954,
    -40.6815920363,
    -44.3855244323,
    -44.2192283342,
    -46.6518280813,
    -44.0782470794,
    -37.3505303693,
    -41.4366859,
    -42.7776602474,
    -42.1784949365,
    -47.1176436157,
    -42.39725139,
    -39.485206922,
    -44.4121958098,
    -44.2147062112,
    -44.6806003599,
    -43.3101086168,
    -39.3394051582,
    -42.5468728275,
    -48.2781956735,
    -46.6278857444,
    -39.0451608224,
    -41.1626742058,
    -45.6666199551,
    -40.0584001747,
    -44.3269096886,
    -50.8026457599,
    -45.4678772753,
    -45.6710031344,
    -46.9507896924,
    -44.7023495278,
    -45.0706731616,
    -44.4995900561,
    -44.2448709593,
    -43.1565888666,
    -48.2749936784,
    -54.6826318605,
    -47.8464275973,
    -45.6977324733,
    -46.4678588459,
    -40.6285301325,
    -43.9165560763,
    -46.6666334449,
    -47.5648493561,
    -52.7689303903,
    -49.1187204017,
    -47.5218774317,
    -50.4402096477,
    -45.8770410664,
    -40.7756638182,
    -40.8779207637,
    -48.5372716972,
    -53.512741862,
    -54.4590035819,
    -60.893607084,
    -51.7923141981,
    -29.8279804839,
    -17.7799641551,
    -7.31026276678,
    -1.18046069995,
    -1.717643155,
    -0.0795351079337,
    -0.607517023058,
    -0.49487481791,
    -0.0531287717291,
    -1.05039010608,
    -0.0125386564567,
    -0.849796067773,
    -0.0733032502755,
    -0.272032092861,
    -0.0454290935607,
    13.3862913391,
    30.9521099785,
    54.5251483892,
    83.3997876021,
    87.9267002711,
    77.9744382885,
    76.3892687313,
    72.3777547916,
    67.0866467587,
    65.5833828648,
    62.405772185,
    59.6419192674,
    57.4091984799,
    54.4836577479,
    52.4930037268,
    50.9177611972,
    49.1730352613,
    47.3622435844,
    46.3597531887,
    45.3560338266,
    43.4755024758,
    42.2954210553,
    41.4652920124,
    40.0509416178,
    38.7785293649,
    38.1118362159,
    37.2790699261,
    35.8259157664,
    35.0206665564,
    34.5677146235,
    33.578638524,
    32.7166398959,
    31.9334129695,
    31.3530723117,
    30.8656421459,
    30.0469780597,
    29.2818690066,
    28.6354809304,
    28.3839383712,
    27.8264143487,
    26.6447715156,
    26.3626572537,
    26.1444162943,
    25.1980634017,
    24.8596164675,
    24.580421254,
    23.6939496758,
    23.2559257638,
    23.226897714,
    22.5752042942,
    21.7337270498,
    21.4399440574,
    21.2918972268,
    20.9178338115,
    20.1898077311,
    19.7967047874,
    19.6258732553,
    18.7484390155,
    18.4646994137,
    18.5174909911,
    17.8346826965,
    17.6776016492,
    17.5002902145,
    17.0126708711,
    16.5765531813,
    15.8122097554,
    15.8020114645,
    15.5086889755,
    14.8006259804,
    15.2321927113,
    14.6699558648,
    13.530409423,
    13.6551951752,
    13.4216482134,
  ]);

  const L1_AUTOPILOT_ANGLES = Object.freeze([
    12.7718080985,
    12.347411389,
    11.9040633729,
    11.4622385295,
    10.9641143117,
    10.4402848282,
    9.86250153799,
    9.23783045957,
    8.51167245519,
    7.69844130884,
    6.72075869987,
    5.51142412922,
    4.02138336542,
    2.04240045078,
    0,
    0,
    0,
    0,
    0,
    -90,
    -90,
    -65.165,
    -46.8261212812,
    -36.2319516504,
    -29.89,
    -26.3232630761,
    -24.513038379,
    -24.1852116838,
    -24.166511011,
    -24.1892558233,
    -24.185,
    -24.214989664,
    -24.5143818046,
    -24.8985556412,
    -25.1972613704,
    -25.493417743,
    -25.8134421412,
    -26.1037318556,
    -26.43,
    -26.637719854,
    -26.9275362052,
    -27.1909480913,
    -27.4876910686,
    -27.76,
    -28.0033393889,
    -28.255,
    -28.5,
    -28.7950639439,
    -29.0573568925,
    -29.2984406838,
    -29.5,
    -29.77,
    -30.04,
    -30.24,
    -30.49,
    -30.695,
    -30.9189929146,
    -31.215,
    -31.415,
    -31.62,
    -31.815,
    -32.1103741627,
    -32.3359976763,
    -32.53,
    -32.6916970036,
    -32.9106817436,
    -33.1543305643,
    -33.3678186649,
    -33.5257675169,
    -33.7831409155,
    -33.9662274793,
    -34.1415571981,
    -34.4064975463,
    -34.6171865666,
    -34.7555140162,
    -34.9022856424,
    -35.1589486753,
    -35.3217359616,
    -35.5168037931,
    -35.7177497932,
    -35.8873962589,
    -36.1038542661,
    -36.3162522153,
    -36.4418888529,
    -36.6458823778,
    -36.8368496899,
    -37.0309052366,
    -37.1888006013,
    -37.342049895,
    -37.5291023557,
    -37.6690580173,
    -37.8844310657,
    -38.0446527887,
    -38.2021272172,
    -38.383778466,
    -38.5851899045,
    -38.7244892637,
    -38.8973011294,
    -39.0047787171,
    -39.2030572121,
    -39.4354517952,
    -39.54,
    -39.68,
    -39.86,
    -40.0101002046,
    -40.1205116742,
    -40.3087822688,
    -40.4753068546,
    -40.5621222315,
    -40.8149888729,
    -40.9030518732,
    -41.0876047818,
    -41.1516032194,
    -41.3941768954,
    -41.5315920363,
    -41.5755244323,
    -41.8642283342,
    -41.945,
    -42.1532470794,
    -42.2355303693,
    -42.3266859,
    -42.5226602474,
    -42.5484949365,
    -42.675,
    -43.02725139,
    -43.040206922,
    -43.22,
    -43.34,
    -43.35,
    -43.36,
    -43.78,
    -43.8168728275,
    -44.03,
    -44.18,
    -44.22,
    -44.26,
    -44.52,
    -44.52,
    -44.78,
    -44.83,
    -44.86,
    -44.98,
    -45.32,
    -45.32,
    -45.49,
    -45.56,
    -45.72,
    -45.77,
    -45.78,
    -45.79,
    -46.02,
    -46.54,
    -46.54,
    -46.54,
    -46.52,
    -46.36,
    -46.36,
    -46.96,
    -46.96,
    -47.84,
    -47.84,
    -47.83,
    -45.41,
    -45.41,
    -48.91,
    -48.92,
    -52.15,
    -52.15,
    -52.15,
    -52.15,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    -0.0125386564567,
    0,
    0,
    0,
    0,
    11.5112913391,
    29.4671099785,
    51.5601483892,
    89.4397876021,
    89.4367002711,
    89.4044382885,
    79.6642687313,
    73.0377547916,
    68.5116467587,
    64.9083828648,
    61.895772185,
    59.2469192674,
    56.9091984799,
    54.7786577479,
    52.8130037268,
    51.0277611972,
    49.3530352613,
    47.7872435844,
    46.3747531887,
    45,
    43.7305024758,
    42.4904210553,
    41.3302920124,
    40.2109416178,
    39.1835293649,
    38.1618362159,
    37.2140699261,
    36.3209157664,
    35.4506665564,
    34.5727146235,
    33.803638524,
    33.0016398959,
    32.2534129695,
    31.5280723117,
    30.8406421459,
    30.1619780597,
    29.5068690066,
    28.8704809304,
    28.2289383712,
    27.6314143487,
    27.0747715156,
    26.4876572537,
    25.9644162943,
    25.4130634017,
    24.8946164675,
    24.385421254,
    23.9139496758,
    23.4409257638,
    22.981897714,
    22.4902042942,
    22.0537270498,
    21.6199440574,
    21.1818972268,
    20.7428338115,
    20.3498077311,
    19.9367047874,
    19.5608732553,
    19.1484390155,
    18.7646994137,
    18.3924909911,
    17.9796826965,
    17.6276016492,
    17.2352902145,
    16.8826708711,
    16.5265531813,
    16.1522097554,
    15.8070114645,
    15.4386889755,
    15.07,
    14.7,
    14.3299558648,
    13.960409423,
    13.5651951752,
    13.1566482134,
  ]);

  const SEGMENTED_STRATEGY_DEFINITIONS = Object.freeze({
    hardSpeed: Object.freeze({
      label: "高速巡航",
      shortLabel: "巡航",
      info: "357 tick · 32.993 b/s · dy +0.000061",
      periodTicks: 357,
      segments: Object.freeze({
        negativeConstantTicks: 10,
        negativeTransitionLinearTicks: 2,
        negativeBezierTicks: 246,
        holdZeroAfterNegativeTicks: 13,
        positiveRampLinearTicks: 3,
        positiveHoldTicks: 5,
        positiveBezierToZeroTicks: 70,
        holdZeroEndTicks: 8,
      }),
      negativeConstantAngle: -89.5,
      negativeBezierControls: Object.freeze([
        -32.84953798913042,
        -40.13245184698956,
        -22.468576627022838,
        -44.7325714278412,
        -49.26187641318844,
        -22.776456702908604,
        -66.27087681959183,
        -38.24817494492914,
      ]),
      positiveBezierControls: Object.freeze([
        78.32703324587365,
        45.31013945331428,
        42.84497193184144,
        16.96999777207822,
        32.377854556287126,
        1.4634826880848515,
        14.955641228893784,
        0,
      ]),
    }),
    maxClimb: Object.freeze({
      label: "最大爬升",
      shortLabel: "爬升",
      info: "254 tick · climb +1.547 b/s · dy +19.653",
      periodTicks: 254,
      segments: Object.freeze({
        negativeConstantTicks: 2,
        negativeTransitionLinearTicks: 5,
        negativeBezierTicks: 141,
        holdZeroAfterNegativeTicks: 15,
        positiveRampLinearTicks: 4,
        positiveHoldTicks: 0,
        positiveBezierToZeroTicks: 83,
        holdZeroEndTicks: 4,
      }),
      negativeConstantAngle: -83.44910138799413,
      negativeBezierControls: Object.freeze([
        -21.507408848406726,
        -31.064412051040822,
        -17.75203519264177,
        -63.83111872689089,
        -5.024003861649917,
        -83.57492395202101,
        -19.020736082912922,
        -62.2599218701962,
      ]),
      positiveBezierControls: Object.freeze([
        80.88135214090949,
        59.766059051049844,
        26.760085044938684,
        33.961915704001306,
        7.911228156821989,
        22.564579953207456,
        10.025017993419707,
        0,
      ]),
    }),
  });

  function appendHold(out, ticks, angle) {
    for (let i = 0; i < ticks; i += 1) {
      out.push(angle);
    }
  }

  function appendLinear(out, ticks, fromAngle, toAngle) {
    for (let i = 0; i < ticks; i += 1) {
      const u = (i + 1) / ticks;
      out.push(fromAngle * (1 - u) + toAngle * u);
    }
  }

  function bezierValue(points, u) {
    const work = Array.from(points);
    for (let level = work.length - 1; level > 0; level -= 1) {
      for (let i = 0; i < level; i += 1) {
        work[i] = work[i] * (1 - u) + work[i + 1] * u;
      }
    }
    return work[0];
  }

  function makeCosineBezierX(count) {
    const controls = [];
    for (let i = 0; i < count; i += 1) {
      controls.push(0.5 - 0.5 * Math.cos((Math.PI * i) / (count - 1)));
    }
    return controls;
  }

  function solveBezierUForX(xControls, targetX) {
    let lo = 0;
    let hi = 1;
    for (let i = 0; i < 60; i += 1) {
      const mid = (lo + hi) * 0.5;
      if (bezierValue(xControls, mid) < targetX) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    return (lo + hi) * 0.5;
  }

  function appendParametricBezier(out, ticks, yControls) {
    if (ticks <= 0) {
      return;
    }
    if (ticks === 1) {
      out.push(yControls[0]);
      return;
    }

    const xControls = makeCosineBezierX(yControls.length);
    for (let i = 0; i < ticks; i += 1) {
      const targetX = i / (ticks - 1);
      const u = solveBezierUForX(xControls, targetX);
      out.push(bezierValue(yControls, u));
    }
  }

  function expandSegmentedStrategy(definition) {
    const s = definition.segments;
    const out = [];
    appendHold(out, s.negativeConstantTicks, definition.negativeConstantAngle);
    appendLinear(
      out,
      s.negativeTransitionLinearTicks,
      definition.negativeConstantAngle,
      definition.negativeBezierControls[0],
    );
    appendParametricBezier(out, s.negativeBezierTicks, definition.negativeBezierControls);
    appendHold(out, s.holdZeroAfterNegativeTicks, 0);
    appendLinear(out, s.positiveRampLinearTicks, 0, definition.positiveBezierControls[0]);
    appendHold(out, s.positiveHoldTicks, definition.positiveBezierControls[0]);
    appendParametricBezier(out, s.positiveBezierToZeroTicks, definition.positiveBezierControls);
    appendHold(out, s.holdZeroEndTicks, 0);

    if (out.length !== definition.periodTicks) {
      throw new Error(`Strategy ${definition.label} expanded to ${out.length} ticks, expected ${definition.periodTicks}.`);
    }
    return Object.freeze(out);
  }

  function buildAutopilotStrategies() {
    return Object.freeze(
      Object.fromEntries(
        Object.entries(SEGMENTED_STRATEGY_DEFINITIONS).map(([id, definition]) => [
          id,
          Object.freeze({
            ...definition,
            angles: expandSegmentedStrategy(definition),
          }),
        ]),
      ),
    );
  }

  function makePlaceholderStrategies() {
    return Object.freeze(
      Object.fromEntries(
        STRATEGY_MANIFEST.map((definition) => [
          definition.id,
          Object.freeze({
            ...definition,
            angles: Object.freeze([0]),
            info: `${definition.info} · 正在加载 CSV`,
          }),
        ]),
      ),
    );
  }

  function parseWaveformCsv(text) {
    const angles = [];
    const lines = text.trim().split(/\r?\n/);
    for (let i = 1; i < lines.length; i += 1) {
      const columns = lines[i].split(",");
      if (columns.length < 2) {
        continue;
      }
      const angle = Number(columns[1]);
      if (Number.isFinite(angle)) {
        angles.push(angle);
      }
    }
    if (angles.length === 0) {
      throw new Error("waveform csv has no angle samples");
    }
    return Object.freeze(angles);
  }

  async function fetchWaveformAngles(fileName) {
    const embeddedKey = fileName.replace(/\/waveform\.csv$/, "");
    const embeddedAngles = EMBEDDED_STRATEGY_WAVEFORMS?.[embeddedKey];
    if (Array.isArray(embeddedAngles) && embeddedAngles.length > 0) {
      return Object.freeze(Array.from(embeddedAngles));
    }

    let lastError = null;
    for (const basePath of STRATEGY_BASE_PATHS) {
      const url = `${basePath}/${fileName}`;
      try {
        const response = await fetch(url, { cache: "no-store" });
        if (!response.ok) {
          lastError = new Error(`${url}: HTTP ${response.status}`);
          continue;
        }
        return parseWaveformCsv(await response.text());
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError || new Error(`Cannot load ${fileName}`);
  }

  async function loadAutopilotStrategies() {
    const entries = await Promise.all(
      STRATEGY_MANIFEST.map(async (definition) => {
        try {
          const angles = await fetchWaveformAngles(definition.fileName);
          return [
            definition.id,
            Object.freeze({
              ...definition,
              angles,
              info: definition.info,
            }),
          ];
        } catch (error) {
          return [
            definition.id,
            Object.freeze({
              ...definition,
              angles: Object.freeze([0]),
              info: `${definition.info} · CSV 加载失败`,
              error,
            }),
          ];
        }
      }),
    );
    AUTOPILOT_STRATEGIES = Object.freeze(Object.fromEntries(entries));
    syncStrategyControls();
    applyAutopilotInput();
  }

  let AUTOPILOT_STRATEGIES = makePlaceholderStrategies();

  const SIN = new Float32Array(65536);
  for (let i = 0; i < SIN.length; i += 1) {
    SIN[i] = Math.fround(Math.sin((i * Math.PI * 2) / SIN.length));
  }

  function mthSin(value) {
    const index = Math.trunc(Math.fround(Math.fround(value) * MC.RAD_TO_INDEX)) & 65535;
    return SIN[index];
  }

  function mthCos(value) {
    const index = Math.trunc(Math.fround(Math.fround(value) * MC.RAD_TO_INDEX + 16384.0)) & 65535;
    return SIN[index];
  }

  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }

  function formatSigned(value, digits = 2) {
    const fixed = value.toFixed(digits);
    return value > 0 ? `+${fixed}` : fixed;
  }

  let width = 1;
  let height = 1;
  let dpr = 1;
  let scale = 4;
  let running = true;
  let autopilotEnabled = false;
  let activeStrategyId = "fromRestGainTwo";
  let lastFrame = performance.now();
  let accumulator = 0;

  const input = {
    noseAngleDeg: 0,
  };

  let state;

  function makeInitialState() {
    return {
      x: 0,
      y: 300,
      vx: 0,
      vy: -0.1,
      ticks: 0,
      durability: MC.ELYTRA_MAX_DURABILITY,
      landed: false,
      trail: [],
    };
  }

  function resetRun() {
    state = makeInitialState();
    input.noseAngleDeg = 0;
    accumulator = 0;
    running = true;
    hud.toggleRun.textContent = "暂停";
    hud.statusBanner.hidden = true;
    syncAutopilotButton();
    syncStrategyControls();
    applyAutopilotInput();
  }

  function resizeCanvas() {
    const rect = canvas.getBoundingClientRect();
    width = Math.max(1, rect.width);
    height = Math.max(1, rect.height);
    dpr = Math.max(1, Math.min(2.5, window.devicePixelRatio || 1));
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  function setPitchFromClientY(clientY) {
    const rect = canvas.getBoundingClientRect();
    const centerY = rect.top + rect.height / 2;
    const normalized = (centerY - clientY) / (rect.height * 0.42);
    input.noseAngleDeg = clamp(normalized * 90, -90, 90);
  }

  function syncAutopilotButton() {
    hud.autoPilot.classList.toggle("is-active", autopilotEnabled);
    hud.autoPilot.textContent = autopilotEnabled ? "手动" : "自动";
    hud.autoPilot.title = autopilotEnabled ? "关闭自动控制，回到鼠标操作" : "按选中的策略自动控制";
  }

  function getActiveAutopilotStrategy() {
    return AUTOPILOT_STRATEGIES[activeStrategyId] || AUTOPILOT_STRATEGIES.hardSpeed;
  }

  function syncStrategyControls() {
    const strategy = getActiveAutopilotStrategy();
    hud.strategyGainTwo.classList.toggle("is-active", activeStrategyId === "fromRestGainTwo");
    hud.strategyReturnHeight.classList.toggle("is-active", activeStrategyId === "fromRestReturnHeight");
    hud.strategySpeed.classList.toggle("is-active", activeStrategyId === "hardSpeed");
    hud.strategyClimb.classList.toggle("is-active", activeStrategyId === "maxClimb");
    hud.strategyInfo.textContent = `${strategy.label} · ${strategy.info}`;
  }

  function setAutopilotStrategy(strategyId) {
    if (!AUTOPILOT_STRATEGIES[strategyId]) {
      return;
    }
    activeStrategyId = strategyId;
    syncStrategyControls();
    if (autopilotEnabled && state) {
      applyAutopilotInput();
    }
  }

  function getAutopilotAngle() {
    const strategy = getActiveAutopilotStrategy();
    return strategy.angles[state.ticks % strategy.angles.length];
  }

  function setAutopilot(enabled) {
    autopilotEnabled = enabled;
    syncAutopilotButton();
    if (autopilotEnabled && state) {
      input.noseAngleDeg = getAutopilotAngle();
    }
  }

  function applyAutopilotInput() {
    if (!autopilotEnabled) {
      return;
    }
    input.noseAngleDeg = getAutopilotAngle();
  }

  function tickElytraPhysics() {
    applyAutopilotInput();

    const mcPitchDeg = clamp(-input.noseAngleDeg, -90, 90);
    const pitchRad = Math.fround(Math.fround(mcPitchDeg) * MC.DEG_TO_RAD);
    const lookH = mthCos(pitchRad);
    const lookY = -mthSin(pitchRad);
    const horizontalLookLength = Math.abs(lookH);
    const lookLength = Math.hypot(horizontalLookLength, lookY);
    const oldHorizontalSpeed = Math.abs(state.vx);
    const lookSign = lookH >= 0 ? 1 : -1;

    let lift = mthCos(pitchRad);
    lift = Math.fround(Math.fround(lift * lift) * Math.min(1.0, lookLength / 0.4));

    let vx = state.vx;
    let vy = state.vy + MC.GRAVITY + lift * MC.LIFT;

    if (vy < 0.0 && horizontalLookLength > 0.0) {
      const yAccel = vy * MC.FALL_TO_GLIDE * lift;
      vy += yAccel;
      vx += lookSign * yAccel;
    }

    if (pitchRad < 0.0 && horizontalLookLength > 0.0) {
      const climb = oldHorizontalSpeed * -mthSin(pitchRad) * MC.PITCH_UP_X;
      vy += climb * MC.PITCH_UP_Y;
      vx -= lookSign * climb;
    }

    if (horizontalLookLength > 0.0) {
      vx += (lookSign * oldHorizontalSpeed - vx) * MC.HORIZONTAL_ALIGN;
    }

    state.vx = vx * MC.HORIZONTAL_DRAG;
    state.vy = vy * MC.VERTICAL_DRAG;
    state.x += state.vx;
    state.y += state.vy;
    state.ticks += 1;

    if (state.ticks % MC.TICKS_PER_SECOND === 0) {
      state.durability = Math.max(1, state.durability - 1);
    }

    state.trail.push({ x: state.x, y: state.y });
    if (state.trail.length > 720) {
      state.trail.shift();
    }

    if (state.y <= 0) {
      state.y = 0;
      state.landed = true;
      running = false;
      hud.toggleRun.textContent = "继续";
      hud.statusBanner.textContent = "着陆";
      hud.statusBanner.hidden = false;
    }
  }

  function updatePhysics(dt) {
    if (!running || state.landed) {
      return;
    }

    accumulator += dt;
    const fixedStep = 1 / MC.TICKS_PER_SECOND;
    let guard = 0;

    while (accumulator >= fixedStep && guard < 8) {
      tickElytraPhysics();
      accumulator -= fixedStep;
      guard += 1;
    }

    if (guard === 8) {
      accumulator = 0;
    }
  }

  function getCamera() {
    scale = clamp(height / 188, width < 680 ? 2.9 : 3.4, width < 680 ? 4.9 : 6.2) / 2;
    const viewW = width / scale;
    const viewH = height / scale;
    return {
      x: state.x - viewW * 0.5,
      y: Math.max(-12, state.y - viewH * 0.5),
    };
  }

  function worldToScreen(camera, x, y) {
    return {
      x: (x - camera.x) * scale,
      y: height - (y - camera.y) * scale,
    };
  }

  function drawSky() {
    const sky = ctx.createLinearGradient(0, 0, 0, height);
    sky.addColorStop(0, "#70c7ff");
    sky.addColorStop(0.55, "#c7edff");
    sky.addColorStop(1, "#effbf3");
    ctx.fillStyle = sky;
    ctx.fillRect(0, 0, width, height);

    ctx.fillStyle = "rgba(255, 248, 188, 0.86)";
    ctx.beginPath();
    ctx.arc(width - 90, 82, 34, 0, Math.PI * 2);
    ctx.fill();
  }

  function drawCloud(camera, x, y, size) {
    const p = worldToScreen(camera, x, y);
    ctx.save();
    ctx.translate(p.x, p.y);
    ctx.scale(size, size);
    ctx.fillStyle = "rgba(255, 255, 255, 0.78)";
    ctx.beginPath();
    ctx.ellipse(-18, 4, 22, 10, 0, 0, Math.PI * 2);
    ctx.ellipse(3, -2, 27, 14, 0, 0, Math.PI * 2);
    ctx.ellipse(28, 4, 20, 10, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  function drawGrid(camera) {
    ctx.save();
    ctx.font = "33px Cascadia Mono, Consolas, monospace";

    const minorStep = 20;
    const majorStep = 100;
    const xMin = Math.floor(camera.x / minorStep) * minorStep;
    const xMax = camera.x + width / scale + minorStep;
    const yMin = Math.max(0, Math.floor(camera.y / minorStep) * minorStep);
    const yMax = camera.y + height / scale + minorStep;
    const groundY = worldToScreen(camera, 0, 0).y;

    ctx.lineWidth = 0.75;
    ctx.strokeStyle = "rgba(22, 64, 79, 0.08)";
    for (let x = xMin; x < xMax; x += minorStep) {
      const sx = worldToScreen(camera, x, 0).x;
      ctx.beginPath();
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, height);
      ctx.stroke();
    }
    for (let y = yMin; y < yMax; y += minorStep) {
      const sy = worldToScreen(camera, 0, y).y;
      ctx.beginPath();
      ctx.moveTo(0, sy);
      ctx.lineTo(width, sy);
      ctx.stroke();
    }

    ctx.lineWidth = 1.5;
    ctx.strokeStyle = "rgba(22, 64, 79, 0.2)";
    ctx.fillStyle = "rgba(22, 64, 79, 0.58)";
    for (let x = Math.floor(camera.x / majorStep) * majorStep; x < xMax; x += majorStep) {
      const sx = worldToScreen(camera, x, 0).x;
      ctx.beginPath();
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, height);
      ctx.stroke();
      if (sx > 72 && sx < width - 96) {
        const labelY = clamp(groundY - 13.5, 46.5, height - 12);
        ctx.fillText(`${x}m`, sx + 7.5, labelY);
      }
    }
    for (let y = Math.floor(yMin / majorStep) * majorStep; y < yMax; y += majorStep) {
      if (y < 0) {
        continue;
      }
      const sy = worldToScreen(camera, 0, y).y;
      ctx.beginPath();
      ctx.moveTo(0, sy);
      ctx.lineTo(width, sy);
      ctx.stroke();
      if (sy > 49.5 && sy < height - 40.5) {
        ctx.fillText(`${y}m`, 13.5, sy - 7.5);
      }
    }

    const axisX = worldToScreen(camera, 0, 0).x;
    const axisY = groundY;
    ctx.lineWidth = 2.7;
    ctx.strokeStyle = "rgba(16, 33, 42, 0.34)";
    if (axisX >= 0 && axisX <= width) {
      ctx.beginPath();
      ctx.moveTo(axisX, 0);
      ctx.lineTo(axisX, height);
      ctx.stroke();
    }
    if (axisY >= 0 && axisY <= height) {
      ctx.beginPath();
      ctx.moveTo(0, axisY);
      ctx.lineTo(width, axisY);
      ctx.stroke();
    }
    ctx.restore();
  }

  function drawGround(camera) {
    const ground = worldToScreen(camera, 0, 0).y;
    if (ground < 0) {
      return;
    }

    const top = clamp(ground, 0, height);
    ctx.fillStyle = "#3b8b4a";
    ctx.fillRect(0, top, width, 12);
    ctx.fillStyle = "#69767a";
    ctx.fillRect(0, top + 12, width, Math.max(0, height - top - 12));

    ctx.save();
    ctx.strokeStyle = "rgba(16, 33, 42, 0.18)";
    ctx.lineWidth = 1;
    const block = 16 * scale;
    const offset = -(((camera.x * scale) % block) + block) % block;
    for (let x = offset; x < width + block; x += block) {
      ctx.beginPath();
      ctx.moveTo(x, top + 12);
      ctx.lineTo(x, height);
      ctx.stroke();
    }
    for (let y = top + 12; y < height; y += block) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }
    ctx.restore();
  }

  function drawTrail(camera) {
    if (state.trail.length < 2) {
      return;
    }

    ctx.save();
    ctx.lineWidth = 2;
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    for (let i = 1; i < state.trail.length; i += 1) {
      const a = worldToScreen(camera, state.trail[i - 1].x, state.trail[i - 1].y);
      const b = worldToScreen(camera, state.trail[i].x, state.trail[i].y);
      ctx.strokeStyle = `rgba(0, 126, 134, ${i / state.trail.length * 0.52})`;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }
    ctx.restore();
  }

  function drawVelocityVector(origin) {
    const len = Math.hypot(state.vx, state.vy);
    if (len < 0.002) {
      return;
    }

    const endX = origin.x + state.vx * 190;
    const endY = origin.y - state.vy * 190;
    const angle = Math.atan2(endY - origin.y, endX - origin.x);

    ctx.save();
    ctx.strokeStyle = "rgba(180, 35, 24, 0.74)";
    ctx.fillStyle = "rgba(180, 35, 24, 0.9)";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(origin.x, origin.y);
    ctx.lineTo(endX, endY);
    ctx.stroke();
    ctx.translate(endX, endY);
    ctx.rotate(angle);
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(-8, -4);
    ctx.lineTo(-8, 4);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
  }

  function drawPlayer(camera) {
    const p = worldToScreen(camera, state.x, state.y + 1.2);
    const noseRad = (input.noseAngleDeg * Math.PI) / 180;

    drawVelocityVector(p);

    ctx.save();
    ctx.translate(p.x, p.y);
    ctx.rotate(-noseRad);

    ctx.fillStyle = "rgba(63, 126, 152, 0.78)";
    ctx.beginPath();
    ctx.moveTo(-3, 0);
    ctx.lineTo(-30, -13);
    ctx.lineTo(-14, 0);
    ctx.lineTo(-30, 13);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = "#17313a";
    ctx.beginPath();
    ctx.moveTo(16, 0);
    ctx.lineTo(-8, -6);
    ctx.lineTo(-13, 0);
    ctx.lineTo(-8, 6);
    ctx.closePath();
    ctx.fill();

    ctx.fillStyle = "#e7f3f1";
    ctx.fillRect(-6, -3, 8, 6);

    ctx.strokeStyle = "rgba(255, 255, 255, 0.72)";
    ctx.lineWidth = 1.4;
    ctx.beginPath();
    ctx.moveTo(-14, -2);
    ctx.lineTo(-28, -10);
    ctx.moveTo(-14, 2);
    ctx.lineTo(-28, 10);
    ctx.stroke();
    ctx.restore();

    ctx.save();
    ctx.strokeStyle = "rgba(16, 33, 42, 0.24)";
    ctx.setLineDash([7, 7]);
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(p.x, p.y);
    ctx.lineTo(p.x + Math.cos(noseRad) * 120, p.y - Math.sin(noseRad) * 120);
    ctx.stroke();
    ctx.restore();
  }

  function draw() {
    const camera = getCamera();
    drawSky();

    for (let i = 0; i < 8; i += 1) {
      const cloudX = Math.floor((camera.x * 0.34) / 210) * 210 + i * 210 + 70;
      drawCloud({ x: camera.x * 0.34, y: camera.y }, cloudX, 142 + Math.sin(i * 1.9) * 24, 0.7 + (i % 3) * 0.13);
    }

    drawGrid(camera);
    drawTrail(camera);
    drawGround(camera);
    drawPlayer(camera);
  }

  function updateHud() {
    const speedBlocksPerSecond = Math.hypot(state.vx, state.vy) * MC.TICKS_PER_SECOND;
    const mcPitch = -input.noseAngleDeg;
    hud.noseAngle.textContent = `${formatSigned(input.noseAngleDeg, 1)} deg`;
    hud.mcPitch.textContent = `${formatSigned(mcPitch, 1)} deg`;
    hud.vx.textContent = `${formatSigned(state.vx, 3)} b/t`;
    hud.vy.textContent = `${formatSigned(state.vy, 3)} b/t`;
    hud.speed.textContent = `${speedBlocksPerSecond.toFixed(2)} m/s`;
    hud.altitude.textContent = state.y.toFixed(1);
    hud.distance.textContent = `${Math.max(0, Math.floor(state.x))} m`;
    hud.ticks.textContent = String(state.ticks);
    hud.durability.textContent = String(state.durability);
    hud.pitchMarker.style.top = `${50 - (input.noseAngleDeg / 180) * 100}%`;
  }

  function frame(now) {
    const dt = Math.min(0.1, (now - lastFrame) / 1000);
    lastFrame = now;
    updatePhysics(dt);
    draw();
    updateHud();
    requestAnimationFrame(frame);
  }

  canvas.addEventListener("pointerdown", (event) => {
    canvas.setPointerCapture(event.pointerId);
    setAutopilot(false);
    setPitchFromClientY(event.clientY);
  });

  canvas.addEventListener("pointermove", (event) => {
    if (autopilotEnabled) {
      return;
    }
    setPitchFromClientY(event.clientY);
  });

  canvas.addEventListener("pointerleave", (event) => {
    if (autopilotEnabled) {
      return;
    }
    if (event.buttons === 0) {
      return;
    }
    setPitchFromClientY(event.clientY);
  });

  window.addEventListener("resize", resizeCanvas);

  window.addEventListener("keydown", (event) => {
    if (event.key.toLowerCase() === "r") {
      resetRun();
    }
    if (event.key.toLowerCase() === "a") {
      setAutopilot(!autopilotEnabled);
    }
    if (event.key === "1") {
      setAutopilotStrategy("fromRestGainTwo");
    }
    if (event.key === "2") {
      setAutopilotStrategy("fromRestReturnHeight");
    }
    if (event.key === "3") {
      setAutopilotStrategy("maxClimb");
    }
    if (event.key === "4") {
      setAutopilotStrategy("hardSpeed");
    }
    if (event.key === " ") {
      event.preventDefault();
      hud.toggleRun.click();
    }
  });

  hud.toggleRun.addEventListener("click", () => {
    if (state.landed) {
      resetRun();
      return;
    }
    running = !running;
    hud.toggleRun.textContent = running ? "暂停" : "继续";
  });

  hud.resetRun.addEventListener("click", resetRun);

  hud.autoPilot.addEventListener("click", () => {
    setAutopilot(!autopilotEnabled);
  });

  hud.strategyGainTwo.addEventListener("click", () => {
    setAutopilotStrategy("fromRestGainTwo");
  });

  hud.strategyReturnHeight.addEventListener("click", () => {
    setAutopilotStrategy("fromRestReturnHeight");
  });

  hud.strategySpeed.addEventListener("click", () => {
    setAutopilotStrategy("hardSpeed");
  });

  hud.strategyClimb.addEventListener("click", () => {
    setAutopilotStrategy("maxClimb");
  });

  hud.formulaToggle.addEventListener("click", () => {
    hud.formulaPanel.classList.toggle("open");
  });

  hud.closeFormula.addEventListener("click", () => {
    hud.formulaPanel.classList.remove("open");
  });

  resizeCanvas();
  resetRun();
  loadAutopilotStrategies();
  requestAnimationFrame(frame);
})();
