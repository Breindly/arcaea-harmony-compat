/** 谱面解析结果（与 napi_init.cpp ParseChart 对应） */
export interface ChartInfo {
  ok: boolean;
  audioOffset: number;
  totalPlayableNotes: number;
  issueCount: number;
  taps: number;
  holds: number;
  arcs: number;
  arcTaps: number;
  timings: number;
  cameras: number;
  sceneControls: number;
  timingGroups: number;
  startBpm: number;
}

/** 播放阶段：0 Idle / 1 Preparing / 2 Ready / 3 Playing / 4 Paused / 5 Finished / 6 Error */
export interface PlayState {
  phase: number;
  time: number;
  duration: number;
  score: number;
  /** Recollection Rate 0..100（Normal/Easy 从 0 起，Hard 从 100 起） */
  gaugeF: number;
  gauge: number;
  trackLost: boolean;
  combo: number;
  maxCombo: number;
  pure: number;
  far: number;
  lost: number;
  total: number;
  judgeSerial: number;
  lastGrade: number;
  lastDelta: number;
  audioReady: boolean;
  rendererReady: boolean;
  surfaceW: number;
  surfaceH: number;
  error: string;
}

export interface PrepareResult {
  ok: boolean;
  error: string;
}

export const parseChart: (text: string) => ChartInfo;
export const coreVersion: () => string;

/** 装载谱面 + 音频文件（OGG/WAV，AVPlayer 异步 prepare）
 *  gaugeKind: 0=Normal 1=Easy 2=Hard；chartConstant=谱面定数（RF 分段用） */
export const prepareSong: (chartText: string, audioPath: string, gaugeKind?: number,
  chartConstant?: number) => PrepareResult;
export const play: () => boolean;
export const pause: () => boolean;
export const restart: () => boolean;
export const stop: () => boolean;
export const setUserOffset: (ms: number) => boolean;
export const setNoteSpeed: (speed: number) => boolean;
export const setBackground: (rgba: ArrayBuffer, width: number, height: number) => boolean;
export const clearBackground: () => boolean;
export const getPlayState: () => PlayState;
