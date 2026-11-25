import { common } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';
import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import { prepareSong, play, pause, restart, stop, getPlayState, setUserOffset, setNoteSpeed,
  setBackground, clearBackground } from 'libarcclient.so';
import { PlayState } from 'libarcclient.so';
import { SongInfo, DiffInfo, LibraryStore } from '../models/Library';
import { SongFiles } from '../services/SongFiles';
import { PlayResult } from '../models/PlayResult';
import { ScoreStore, SettingsStore } from '../services/Stores';

@Component
export struct PlayScreen {
  song: SongInfo = new SongInfo();
  diff: DiffInfo = new DiffInfo();
  onExit: () => void = () => {};
  onFinish: (result: PlayResult) => void = () => {};

  @StorageLink('arc.appHidden') @Watch('onAppHiddenChanged') appHidden: boolean = false;

  @State phase: number = 1;
  @State timeMs: number = 0;
  @State durationMs: number = 0;
  @State combo: number = 0;
  @State maxCombo: number = 0;
  @State pure: number = 0;
  @State far: number = 0;
  @State lost: number = 0;
  @State total: number = 0;
  @State score: number = 0;
  @State paused: boolean = false;
  @State errorText: string = '';
  @State userOffset: number = 0;
  @State noteSpeed: number = 3;
  @State gaugeKind: number = 0; // 0=Normal 1=Easy 2=Hard
  @State gauge: number = 0; // Recollection Rate 0..100
  @State trackLost: boolean = false;
  @State judgeLabel: string = '';
  @State judgeDetail: string = '';
  @State judgeColor: string = '#ffffff';
  @State judgeVisible: boolean = false;

  private timer: number = -1;
  private judgeTimer: number = -1;
  private lastJudgeSerial: number = 0;
  private started: boolean = false;
  private finishing: boolean = false;
  private ctx: common.UIAbilityContext | null = null;
  private scores: ScoreStore | null = null;
  private settings: SettingsStore | null = null;

  aboutToAppear(): void {
    this.ctx = this.getUIContext().getHostContext() as common.UIAbilityContext;
    this.begin();
  }

  aboutToDisappear(): void {
    if (this.timer >= 0) {
      clearInterval(this.timer);
      this.timer = -1;
    }
    if (this.judgeTimer >= 0) {
      clearTimeout(this.judgeTimer);
      this.judgeTimer = -1;
    }
    stop();
    if (this.ctx !== null) {
      window.getLastWindow(this.ctx).then((w: window.Window) => {
        w.setWindowKeepScreenOn(false);
      }).catch(() => {
      });
    }
  }

  private async loadBackground(): Promise<void> {
    if (this.ctx === null) return;
    clearBackground();
    let src: image.ImageSource | null = null;
    let pm: image.PixelMap | null = null;
    try {
      const data: Uint8Array =
        await this.ctx.resourceManager.getRawFileContent(`library/jackets/${this.song.jacket}`);
      const buf: ArrayBuffer = data.buffer.slice(0);
      const source: image.ImageSource = image.createImageSource(buf);
      src = source;
      const opts: image.DecodingOptions = {
        desiredPixelFormat: image.PixelMapFormat.RGBA_8888,
        desiredSize: { width: 384, height: 384 }
      };
      const pixelMap: image.PixelMap = await source.createPixelMap(opts);
      pm = pixelMap;
      const rgba: ArrayBuffer = new ArrayBuffer(384 * 384 * 4);
      await pixelMap.readPixelsToBuffer(rgba);
      setBackground(rgba, 384, 384);
    } catch (e) {
      console.info('background unavailable for this song');
    } finally {
      if (pm !== null) {
        await pm.release();
      }
      if (src !== null) {
        await src.release();
      }
    }
  }

  private async begin(): Promise<void> {
    if (this.ctx === null) {
      this.errorText = '无法获取应用上下文';
      return;
    }
    this.loadBackground();
    const files: SongFiles = new SongFiles(this.ctx);
    const lib: LibraryStore = new LibraryStore(this.ctx);
    this.scores = new ScoreStore(this.ctx);
    this.settings = new SettingsStore(this.ctx);
    this.userOffset = this.settings.getOffset();
    setUserOffset(this.userOffset);
    this.noteSpeed = this.settings.getSpeed();
    setNoteSpeed(this.noteSpeed);
    this.gaugeKind = this.settings.getGaugeKind();
    const audioPath: string = files.audioPath(this.song, this.diff);
    try {
      const chart: string = await lib.readChart(this.song.id, this.diff.rc);
      const rating: number = parseFloat(this.diff.rating);
      const res = prepareSong(chart, audioPath, this.gaugeKind, isNaN(rating) ? 0 : rating);
      if (!res.ok) {
        this.errorText = res.error.length > 0 ? res.error : '谱面或音频加载失败';
        return;
      }
      window.getLastWindow(this.ctx).then((w: window.Window) => {
        w.setWindowKeepScreenOn(true);
      }).catch(() => {
      });
      this.timer = setInterval(() => {
        this.poll();
      }, 80);
    } catch (e) {
      const err = e as BusinessError;
      console.error(`play begin failed: code=${err.code} msg=${err.message}`);
      this.errorText = `读取谱面失败: ${err.code} ${err.message}`;
    }
  }

  private poll(): void {
    const st: PlayState = getPlayState();
    this.phase = st.phase;
    this.timeMs = st.time;
    this.durationMs = st.duration;
    this.combo = st.combo;
    this.maxCombo = st.maxCombo;
    this.pure = st.pure;
    this.far = st.far;
    this.lost = st.lost;
    this.total = st.total;
    this.score = st.score;
    this.gauge = st.gaugeF;
    this.trackLost = st.trackLost;
    if (st.judgeSerial !== this.lastJudgeSerial) {
      this.lastJudgeSerial = st.judgeSerial;
      this.showJudge(st.lastGrade, st.lastDelta);
    }

    if (st.phase === 2 && st.rendererReady && !this.started) {
      this.started = true;
      play();
    }
    if (st.phase === 6) {
      this.errorText = st.error.length > 0 ? st.error : '音频加载失败';
    }
    if (st.phase === 5 && !this.finishing) {
      this.finishing = true;
      if (this.timer >= 0) {
        clearInterval(this.timer);
        this.timer = -1;
      }
      const prev: number = this.scores !== null ? this.scores.get(this.song.id, this.diff.rc) : 0;
      if (this.scores !== null && !st.trackLost) {
        this.scores.put(this.song.id, this.diff.rc, st.score);
      }
      const result = new PlayResult();
      result.score = st.score;
      result.pure = st.pure;
      result.far = st.far;
      result.lost = st.lost;
      result.maxCombo = st.maxCombo;
      result.total = st.total;
      result.duration = st.duration;
      result.newBest = !st.trackLost && st.score > prev;
      result.trackLost = st.trackLost;
      result.gauge = st.gauge;
      this.onFinish(result);
    }
  }

  private diffLabel(): string {
    if (this.diff.rc === 0) return 'PAST';
    if (this.diff.rc === 1) return 'PRESENT';
    if (this.diff.rc === 2) return 'FUTURE';
    if (this.diff.rc === 3) return 'BEYOND';
    return 'ETERNAL';
  }

  private diffColor(): string {
    if (this.diff.rc === 0) return '#5b8def';
    if (this.diff.rc === 1) return '#4fc3a1';
    if (this.diff.rc === 2) return '#b06ad6';
    if (this.diff.rc === 3) return '#e05555';
    return '#d8a24a';
  }

  private formatScore(v: number): string {
    let s: string = `${v}`;
    let out: string = '';
    let count: number = 0;
    for (let i = s.length - 1; i >= 0; i--) {
      out = s.charAt(i) + out;
      count++;
      if (count % 3 === 0 && i > 0) out = ',' + out;
    }
    return out;
  }

  onAppHiddenChanged(): void {
    if (this.appHidden && this.phase === 3) {
      this.doPause();
    }
  }

  private doPause(): void {
    if (this.phase !== 3) return;
    pause();
    this.paused = true;
  }

  private doResume(): void {
    this.paused = false;
    play();
  }

  private doRestart(): void {
    this.paused = false;
    this.finishing = false;
    this.started = true;
    this.lastJudgeSerial = 0;
    this.judgeVisible = false;
    this.combo = 0;
    this.maxCombo = 0;
    this.pure = 0;
    this.far = 0;
    this.lost = 0;
    this.score = 0;
    restart();
  }

  private doExit(): void {
    stop();
    this.onExit();
  }

  private showJudge(grade: number, delta: number): void {
    if (grade <= 0) return;
    if (grade === 1) {
      this.judgeLabel = 'PURE';
      this.judgeColor = '#9fd8ff';
    } else if (grade === 2) {
      this.judgeLabel = 'FAR';
      this.judgeColor = '#ffd66b';
    } else {
      this.judgeLabel = 'LOST';
      this.judgeColor = '#ff7a7a';
    }
    let detail: string = '';
    if (grade !== 3) {
      const abs: number = Math.abs(delta);
      if (abs >= 6) {
        detail = delta > 0 ? `EARLY ${abs}ms` : `LATE ${abs}ms`;
      }
    }
    this.judgeDetail = detail;
    this.judgeVisible = true;
    if (this.judgeTimer >= 0) {
      clearTimeout(this.judgeTimer);
    }
    this.judgeTimer = setTimeout(() => {
      this.judgeVisible = false;
      this.judgeTimer = -1;
    }, 480);
  }

  private adjustSpeed(delta: number): void {
    let next: number = Math.round((this.noteSpeed + delta) * 10) / 10;
    if (next < 1) next = 1;
    if (next > 6.5) next = 6.5;
    this.noteSpeed = next;
    setNoteSpeed(next);
    if (this.settings !== null) this.settings.setSpeed(next);
  }

  private adjustOffset(delta: number): void {
    let next: number = this.userOffset + delta;
    if (next < -300) next = -300;
    if (next > 300) next = 300;
    this.userOffset = next;
    setUserOffset(next);
    if (this.settings !== null) this.settings.setOffset(next);
  }

  private applyGaugeKind(kind: number): void {
    this.gaugeKind = kind;
    if (this.settings !== null) this.settings.setGaugeKind(kind);
  }

  private onBackPressed(): void {
    if (this.phase === 3) {
      this.doPause();
    } else if (this.phase === 4) {
      this.doResume();
    } else {
      this.doExit();
    }
  }

  private onPausePressed(): void {
    if (this.phase === 3) {
      this.doPause();
    } else if (this.phase === 4) {
      this.doResume();
    }
  }

  @Builder
  TopBar() {
    Row({ space: 10 }) {
      Button('‹')
        .fontSize(24)
        .width(38)
        .height(38)
        .backgroundColor('#cc141a25')
        .fontColor('#dce6f5')
        .onClick(() => {
          this.onBackPressed();
        })
      Column({ space: 1 }) {
        Text(this.song.title)
          .fontSize(13)
          .fontWeight(FontWeight.Medium)
          .fontColor('#eef4ff')
          .maxLines(1)
        Text(`${this.diffLabel()} ${this.diff.rating}`)
          .fontSize(10)
          .fontColor(this.diffColor())
      }
      .alignItems(HorizontalAlign.Start)
      .layoutWeight(1)
      Text(this.formatScore(this.score))
        .fontSize(15)
        .fontWeight(FontWeight.Bold)
        .fontColor('#f3f7ff')
      Button('❚❚')
        .fontSize(13)
        .width(38)
        .height(34)
        .backgroundColor('#cc141a25')
        .fontColor('#dce6f5')
        .onClick(() => {
          this.onPausePressed();
        })
    }
    .width('100%')
    .padding({ left: 12, right: 12, top: 10 })
  }

  @Builder
  GaugeBar() {
    Row({ space: 8 }) {
      Text('GAUGE')
        .fontSize(9)
        .fontColor('#7d8aa0')
      Stack({ alignContent: Alignment.Start }) {
        // 底槽
        Row()
          .width('100%')
          .height(4)
          .borderRadius(2)
          .backgroundColor('#22ffffff')
        // Current RR fill: purple below 70, cyan at or above 70, red when Hard is low.
        Row()
          .width(`${Math.max(0, Math.min(100, this.gauge))}%`)
          .height(4)
          .borderRadius(2)
          .backgroundColor(this.gaugeColor())
        // 清线标记：Normal/Easy 70%，Hard 30% 红闪线（layoutWeight 定位）
        Row() {
          Blank().layoutWeight(this.gaugeKind === 2 ? 30 : 70)
          Row()
            .width(2)
            .height(8)
            .backgroundColor(this.gaugeKind === 2 ? '#ff5a5a' : '#ffffff')
            .opacity(this.gaugeKind === 2 ? 0.9 : 0.55)
          Blank().layoutWeight(this.gaugeKind === 2 ? 70 : 30)
        }
        .width('100%')
        .height(10)
      }
      .layoutWeight(1)
      .height(10)
      Text(this.gaugeKind === 2 ? `${this.gauge.toFixed(0)}%` : `${this.gauge.toFixed(1)}%`)
        .fontSize(9)
        .fontColor(this.gaugeFontColor())
    }
    .width('100%')
    .padding({ left: 16, right: 16, top: 4 })
  }

  private gaugeColor(): string {
    if (this.gaugeKind === 2) {
      return this.gauge <= 30 ? '#ff5a5a' : '#9a6ad6';
    }
    return this.gauge >= 70 ? '#7fd4ff' : '#9a6ad6';
  }

  private gaugeFontColor(): string {
    if (this.gaugeKind === 2 && this.gauge <= 30) return '#ff8a8a';
    if (this.gaugeKind !== 2 && this.gauge >= 70) return '#7fd4ff';
    return '#7d8aa0';
  }

  @Builder
  ScoreOverlay() {
    Column({ space: 2 }) {
      if (this.combo > 2) {
        Text(`${this.combo}`)
          .fontSize(36)
          .fontWeight(FontWeight.Bold)
          .fontColor('#ffffff')
        Text('COMBO')
          .fontSize(10)
          .fontColor('#aab7cc')
      }
      Row({ space: 12 }) {
        Text(`P ${this.pure}`).fontSize(12).fontColor('#7fd4ff')
        Text(`F ${this.far}`).fontSize(12).fontColor('#ffd66b')
        Text(`L ${this.lost}`).fontSize(12).fontColor('#ff7a7a')
      }
      Text(`${this.pure + this.far + this.lost}/${this.total}`)
        .fontSize(10)
        .fontColor('#6b7280')
    }
    .alignItems(HorizontalAlign.Center)
    .position({ x: '72%', y: 92 })
  }

  @Builder
  PausePanel() {
    Column({ space: 14 }) {
      Text('PAUSED')
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
        .fontColor('#f2f7ff')
      Text(`${this.song.title} · ${this.diffLabel()} ${this.diff.rating}`)
        .fontSize(12)
        .fontColor('#8e9bb0')
      Row({ space: 12 }) {
        Button('-10')
          .width(58)
          .height(30)
          .fontSize(12)
          .backgroundColor('#1a2435')
          .fontColor('#cbd8ea')
          .onClick(() => {
            this.adjustOffset(-10);
          })
        Text(`音频偏移 ${this.userOffset} ms`)
          .fontSize(12)
          .fontColor('#9fb0c8')
        Button('+10')
          .width(58)
          .height(30)
          .fontSize(12)
          .backgroundColor('#1a2435')
          .fontColor('#cbd8ea')
          .onClick(() => {
            this.adjustOffset(10);
          })
      }
      .margin({ top: 2 })
      Row({ space: 12 }) {
        Button('-')
          .width(58)
          .height(30)
          .fontSize(14)
          .backgroundColor('#1a2435')
          .fontColor('#cbd8ea')
          .onClick(() => {
            this.adjustSpeed(-0.5);
          })
        Text(`音符流速 ${this.noteSpeed.toFixed(1)}`)
          .fontSize(12)
          .fontColor('#9fb0c8')
        Button('+')
          .width(58)
          .height(30)
          .fontSize(14)
          .backgroundColor('#1a2435')
          .fontColor('#cbd8ea')
          .onClick(() => {
            this.adjustSpeed(0.5);
          })
      }
      Row({ space: 8 }) {
        Text('血条')
          .fontSize(12)
          .fontColor('#9fb0c8')
        Button('NORMAL')
          .width(78)
          .height(30)
          .fontSize(11)
          .backgroundColor(this.gaugeKind === 0 ? '#2b3f66' : '#1a2435')
          .fontColor(this.gaugeKind === 0 ? '#7fd4ff' : '#cbd8ea')
          .onClick(() => {
            this.applyGaugeKind(0);
          })
        Button('EASY')
          .width(64)
          .height(30)
          .fontSize(11)
          .backgroundColor(this.gaugeKind === 1 ? '#2b3f66' : '#1a2435')
          .fontColor(this.gaugeKind === 1 ? '#7fd4ff' : '#cbd8ea')
          .onClick(() => {
            this.applyGaugeKind(1);
          })
        Button('HARD')
          .width(64)
          .height(30)
          .fontSize(11)
          .backgroundColor(this.gaugeKind === 2 ? '#4a2333' : '#1a2435')
          .fontColor(this.gaugeKind === 2 ? '#ff8a8a' : '#cbd8ea')
          .onClick(() => {
            this.applyGaugeKind(2);
          })
      }
      Text('血条模式从下一次游玩生效')
        .fontSize(9)
        .fontColor('#5d6a80')
      Button('继续')
        .width(190)
        .height(44)
        .fontSize(15)
        .backgroundColor('#2b3f66')
        .fontColor('#eaf2ff')
        .onClick(() => {
          this.doResume();
        })
      Button('重新开始')
        .width(190)
        .height(44)
        .fontSize(15)
        .backgroundColor('#1a2435')
        .fontColor('#cbd8ea')
        .onClick(() => {
          this.doRestart();
        })
      Button('退出曲目')
        .width(190)
        .height(44)
        .fontSize(15)
        .backgroundColor('#161d29')
        .fontColor('#9fabbd')
        .onClick(() => {
          this.doExit();
        })
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .backgroundColor('#c8000000')
  }

  build() {
    Stack() {
      XComponent({ id: 'playfield', type: XComponentType.SURFACE, libraryname: 'arcclient' })
        .width('100%')
        .height('100%')

      Column() {
        this.TopBar()
        if (!this.trackLost) {
          this.GaugeBar()
        }
        if (this.judgeVisible) {
          Column({ space: 1 }) {
            Text(this.judgeLabel)
              .fontSize(17)
              .fontWeight(FontWeight.Bold)
              .fontColor(this.judgeColor)
            if (this.judgeDetail.length > 0) {
              Text(this.judgeDetail)
                .fontSize(9)
                .fontColor('#8fa2bd')
            }
          }
          .width('100%')
          .alignItems(HorizontalAlign.Center)
          .margin({ top: 5 })
        }
        Blank()
        Column({ space: 4 }) {
          Progress({ value: Math.max(0, this.timeMs), total: Math.max(1, this.durationMs),
            type: ProgressType.Linear })
            .width('100%')
            .height(3)
            .color('#9fc2ff')
            .backgroundColor('#22ffffff')
          Text(`${Math.max(0, Math.floor(this.timeMs / 1000))}s / ${Math.floor(this.durationMs / 1000)}s`)
            .fontSize(9)
            .fontColor('#79879c')
            .width('100%')
            .textAlign(TextAlign.End)
        }
        .padding({ left: 16, right: 16, bottom: 10 })
      }
      .width('100%')
      .height('100%')

      this.ScoreOverlay()

      if (this.phase === 1 && this.errorText.length === 0) {
        Column({ space: 10 }) {
          LoadingProgress().width(40).height(40).color('#9ec4ff')
          Text('正在准备音频…').fontSize(13).fontColor('#d6e2f5')
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor('#b8000000')
      }

      if (this.errorText.length > 0) {
        Column({ space: 12 }) {
          Text('无法开始').fontSize(20).fontWeight(FontWeight.Bold).fontColor('#ffb4a8')
          Text(this.errorText)
            .fontSize(12)
            .fontColor('#c7d2e2')
            .textAlign(TextAlign.Center)
            .padding({ left: 32, right: 32 })
          Button('返回曲库')
            .width(160)
            .height(40)
            .backgroundColor('#22304a')
            .fontColor('#dbe8fb')
            .onClick(() => {
              this.doExit();
            })
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor('#d8000000')
      }

      if (this.paused) {
        this.PausePanel()
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#05060a')
  }
}
