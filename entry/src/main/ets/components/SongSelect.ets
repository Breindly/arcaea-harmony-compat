import { common } from '@kit.AbilityKit';
import { picker } from '@kit.CoreFileKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { SongInfo, DiffInfo, LibraryStore, PackInfo } from '../models/Library';
import { SongFiles } from '../services/SongFiles';
import { ScoreStore, SettingsStore } from '../services/Stores';

class SongDataSource implements IDataSource {
  private list: SongInfo[] = [];
  private listeners: DataChangeListener[] = [];

  totalCount(): number {
    return this.list.length;
  }

  getData(index: number): SongInfo {
    return this.list[index];
  }

  registerDataChangeListener(listener: DataChangeListener): void {
    if (this.listeners.indexOf(listener) < 0) {
      this.listeners.push(listener);
    }
  }

  unregisterDataChangeListener(listener: DataChangeListener): void {
    const pos: number = this.listeners.indexOf(listener);
    if (pos >= 0) {
      this.listeners.splice(pos, 1);
    }
  }

  setData(list: SongInfo[]): void {
    this.list = list;
    for (const listener of this.listeners) {
      listener.onDataReloaded();
    }
  }
}

@Component
export struct SongSelect {
  onStart: (song: SongInfo, diff: DiffInfo) => void = () => {};

  @State songs: SongInfo[] = [];
  @State filtered: SongInfo[] = [];
  @State loading: boolean = true;
  @State query: string = '';
  @State diffFilter: number = -1;
  @State selected: SongInfo = new SongInfo();
  @State showSheet: boolean = false;
  @State showPacks: boolean = false;
  @State packFilter: string = '';
  @State packs: PackInfo[] = [];
  @State onlyFavorites: boolean = false;
  @State sortMode: number = 0;
  @State selectedFavorite: boolean = false;
  @State available: number = 0;
  @State status: string = '';
  @State importing: boolean = false;
  @State importDone: number = 0;
  @State importTotal: number = 0;

  private data: SongDataSource = new SongDataSource();
  private ctx: common.UIAbilityContext | null = null;
  private files: SongFiles | null = null;
  private lib: LibraryStore | null = null;
  private scores: ScoreStore | null = null;
  private settings: SettingsStore | null = null;

  aboutToAppear(): void {
    this.init();
  }

  private async init(): Promise<void> {
    const host = this.getUIContext().getHostContext();
    if (host === undefined) {
      this.status = '无法获取应用上下文';
      this.loading = false;
      return;
    }
    this.ctx = host as common.UIAbilityContext;
    this.lib = new LibraryStore(this.ctx);
    this.files = new SongFiles(this.ctx);
    try {
      this.songs = await this.lib.load();
      const scores = new ScoreStore(this.ctx);
      this.scores = scores;
      for (const song of this.songs) {
        for (const d of song.diffs) {
          d.bestScore = scores.get(song.id, d.rc);
        }
      }
      const counts: Map<string, number> = new Map<string, number>();
      for (const song of this.songs) {
        counts.set(song.pack, (counts.get(song.pack) ?? 0) + 1);
      }
      const packList: PackInfo[] = [];
      counts.forEach((count: number, name: string) => {
        packList.push(new PackInfo(name, count));
      });
      packList.sort((a: PackInfo, b: PackInfo) => a.name < b.name ? -1 : (a.name > b.name ? 1 : 0));
      this.packs = packList;
      const settings = new SettingsStore(this.ctx);
      this.settings = settings;
      const favIds: Set<string> = new Set<string>();
      for (const id of settings.getFavorites().split(',')) {
        if (id.length > 0) favIds.add(id);
      }
      for (const song of this.songs) {
        song.favorite = favIds.has(song.id);
      }
      await this.files.ensureDemo(this.songs);
      this.available = this.files.scan(this.songs);
      this.applyFilter();
    } catch (e) {
      const err = e as BusinessError;
      this.status = `曲库加载失败: ${err.message}`;
    }
    this.loading = false;
  }

  private applyFilter(): void {
    const q: string = this.query.trim().toLowerCase();
    const out: SongInfo[] = [];
    for (const song of this.songs) {
      if (this.onlyFavorites && !song.favorite) continue;
      if (this.packFilter.length > 0 && song.pack !== this.packFilter) continue;
      if (this.diffFilter >= 0 && !song.hasDiff(this.diffFilter)) continue;
      if (q.length > 0) {
        const hay: string =
          `${song.title} ${song.titleZh} ${song.artist} ${song.id}`.toLowerCase();
        if (hay.indexOf(q) < 0) continue;
      }
      out.push(song);
    }
    if (this.sortMode === 1) {
      out.sort((a: SongInfo, b: SongInfo) =>
        a.title < b.title ? -1 : (a.title > b.title ? 1 : 0));
    } else if (this.sortMode === 2) {
      out.sort((a: SongInfo, b: SongInfo) => a.bpm - b.bpm);
    }
    this.filtered = out;
    this.data.setData(out);
  }

  private sortLabel(): string {
    if (this.sortMode === 1) return '标题';
    if (this.sortMode === 2) return 'BPM';
    return '默认';
  }

  private toggleFavorite(song: SongInfo): void {
    song.favorite = !song.favorite;
    this.selectedFavorite = song.favorite;
    const ids: string[] = [];
    for (const s of this.songs) {
      if (s.favorite) ids.push(s.id);
    }
    if (this.settings !== null) this.settings.setFavorites(ids.join(','));
    this.applyFilter();
  }

  private refreshAudio(): void {
    if (this.files === null) return;
    this.available = this.files.scan(this.songs);
    this.applyFilter();
    this.status = `已检测到 ${this.available} 首本地音频`;
  }

  private async importLibrary(): Promise<void> {
    if (this.ctx === null || this.files === null || this.importing) return;
    try {
      const p = new picker.DocumentViewPicker(this.ctx);
      const opts = new picker.DocumentSelectOptions();
      opts.maxSelectNumber = 1;
      opts.fileSuffixFilters = ['曲库压缩包|.zip'];
      const uris: string[] = await p.select(opts);
      if (uris.length === 0) return;
      const before: number = this.available;
      this.importing = true;
      this.status = '准备导入…';
      await this.files.importZip(uris[0], (text: string) => {
        this.status = text;
      });
      this.importing = false;
      this.available = this.files.scan(this.songs);
      this.applyFilter();
      const added: number = this.available - before;
      this.status = added > 0
        ? `导入完成，本次新增 ${added} 首可用音频（共 ${this.available} 首）`
        : `解压完成，但没有识别到新的音频（共 ${this.available} 首）`;
    } catch (e) {
      this.importing = false;
      const err = e as BusinessError;
      const msg: string = err.message.length > 0 ? err.message : JSON.stringify(e);
      this.status = `导入失败: ${msg}`;
    }
  }

  private hideKeyboard(): void {
    try {
      this.getUIContext().getFocusController().clearFocus();
    } catch (e) {
    }
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

  private diffLabel(rc: number): string {
    if (rc === 0) return 'PST';
    if (rc === 1) return 'PRS';
    if (rc === 2) return 'FTR';
    if (rc === 3) return 'BYD';
    return 'ETR';
  }

  private diffColor(rc: number): string {
    if (rc === 0) return '#5b8def';
    if (rc === 1) return '#4fc3a1';
    if (rc === 2) return '#b06ad6';
    if (rc === 3) return '#e05555';
    return '#d8a24a';
  }

  @Builder
  FavoriteChip() {
    Text(this.onlyFavorites ? '♥ 收藏' : '♡ 收藏')
      .fontSize(12)
      .fontColor(this.onlyFavorites ? '#ff9ab0' : '#8892a6')
      .padding({ left: 12, right: 12, top: 6, bottom: 6 })
      .borderRadius(14)
      .backgroundColor(this.onlyFavorites ? '#3a1f28' : '#141924')
      .onClick(() => {
        this.onlyFavorites = !this.onlyFavorites;
        this.applyFilter();
      })
  }

  @Builder
  SortChip() {
    Text(`排序 ${this.sortLabel()}`)
      .fontSize(12)
      .fontColor('#9fb0c8')
      .padding({ left: 12, right: 12, top: 6, bottom: 6 })
      .borderRadius(14)
      .backgroundColor('#141924')
      .onClick(() => {
        this.sortMode = (this.sortMode + 1) % 3;
        this.applyFilter();
      })
  }

  @Builder
  PackChip() {
    Text(this.packFilter.length > 0 ? this.packFilter : '曲包')
      .fontSize(12)
      .fontColor(this.packFilter.length > 0 ? '#ffe3a3' : '#8892a6')
      .padding({ left: 12, right: 12, top: 6, bottom: 6 })
      .borderRadius(14)
      .backgroundColor(this.packFilter.length > 0 ? '#3a2f14' : '#141924')
      .maxLines(1)
      .textOverflow({ overflow: TextOverflow.Ellipsis })
      .constraintSize({ maxWidth: 168 })
      .onClick(() => {
        this.showPacks = true;
      })
  }

  @Builder
  PackRow(name: string, count: number) {
    Row() {
      Text(name.length > 0 ? name : '全部曲包')
        .fontSize(13)
        .fontColor(this.packFilter === name ? '#f5f9ff' : '#c8d3e2')
        .layoutWeight(1)
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
      Text(`${count}`)
        .fontSize(11)
        .fontColor(this.packFilter === name ? '#ffe3a3' : '#7d8aa0')
    }
    .width('100%')
    .padding({ left: 14, right: 14, top: 11, bottom: 11 })
    .borderRadius(9)
    .backgroundColor(this.packFilter === name ? '#2b3a5c' : '#151b26')
    .onClick(() => {
      this.packFilter = name;
      this.showPacks = false;
      this.applyFilter();
    })
  }

  @Builder
  PackSheet() {
    Column() {
      Blank()
        .layoutWeight(1)
        .width('100%')
        .onClick(() => {
          this.showPacks = false;
        })
      Column({ space: 10 }) {
        Row() {
          Text('选择曲包')
            .fontSize(19)
            .fontWeight(FontWeight.Bold)
            .fontColor('#f5f8ff')
          Blank()
          Button('关闭')
            .fontSize(12)
            .height(30)
            .backgroundColor('#1b2230')
            .fontColor('#9aa5b8')
            .onClick(() => {
              this.showPacks = false;
            })
        }
        .width('100%')
        Text(`${this.packs.length} 个曲包 · ${this.songs.length} 首`)
          .fontSize(11)
          .fontColor('#6e788b')
          .width('100%')
        Scroll() {
          Column({ space: 6 }) {
            this.PackRow('', this.songs.length)
            ForEach(this.packs, (p: PackInfo) => {
              this.PackRow(p.name, p.count)
            }, (p: PackInfo) => p.name)
          }
          .width('100%')
        }
        .layoutWeight(1)
        .scrollBar(BarState.Off)
        .width('100%')
      }
      .width('100%')
      .height('64%')
      .padding({ left: 18, right: 18, top: 18, bottom: 24 })
      .backgroundColor('#111722')
      .borderRadius({ topLeft: 18, topRight: 18 })
      .onClick(() => {
      })
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#99000000')
  }

  @Builder
  FilterChip(label: string, value: number) {
    Text(label)
      .fontSize(12)
      .fontColor(this.diffFilter === value ? '#f5f9ff' : '#8892a6')
      .padding({ left: 12, right: 12, top: 6, bottom: 6 })
      .borderRadius(14)
      .backgroundColor(this.diffFilter === value ? '#2b3a5c' : '#141924')
      .onClick(() => {
        this.diffFilter = value;
        this.applyFilter();
      })
  }

  @Builder
  DiffBadge(d: DiffInfo) {
    Text(`${this.diffLabel(d.rc)} ${d.rating}`)
      .fontSize(10)
      .fontWeight(FontWeight.Medium)
      .fontColor('#ffffff')
      .padding({ left: 6, right: 6, top: 2, bottom: 2 })
      .borderRadius(5)
      .backgroundColor(this.diffColor(d.rc))
  }

  @Builder
  SongCard(song: SongInfo) {
    Column({ space: 5 }) {
      Stack() {
        Image($rawfile(`library/jackets/${song.jacket}`))
          .width('100%')
          .aspectRatio(1)
          .objectFit(ImageFit.Cover)
          .borderRadius(10)
        if (!song.hasAudio) {
          Text('未导入')
            .fontSize(10)
            .fontColor('#ffd48a')
            .padding({ left: 6, right: 6, top: 3, bottom: 3 })
            .backgroundColor('#aa000000')
            .borderRadius(5)
            .position({ x: 6, y: 6 })
        }
      }
      .width('100%')
      .borderRadius(10)
      .clip(true)

      Text(song.title)
        .fontSize(14)
        .fontWeight(FontWeight.Medium)
        .fontColor('#f1f5fb')
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
      Text(song.artist)
        .fontSize(11)
        .fontColor('#818b9d')
        .maxLines(1)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .width('100%')
      Row({ space: 4 }) {
        ForEach(song.diffs, (d: DiffInfo) => {
          this.DiffBadge(d)
        }, (d: DiffInfo) => `${song.id}-${d.rc}`)
      }
      .width('100%')
    }
    .alignItems(HorizontalAlign.Start)
    .onClick(() => {
      this.hideKeyboard();
      this.selected = song;
      this.selectedFavorite = song.favorite;
      this.showSheet = true;
    })
  }

  @Builder
  DiffSheet(song: SongInfo) {
    Column() {
      Blank()
        .layoutWeight(1)
        .width('100%')
        .onClick(() => {
          this.showSheet = false;
        })
      Column({ space: 10 }) {
        Row() {
          Column({ space: 3 }) {
            Text(song.title)
              .fontSize(19)
              .fontWeight(FontWeight.Bold)
              .fontColor('#f5f8ff')
              .maxLines(1)
              .textOverflow({ overflow: TextOverflow.Ellipsis })
            Text(song.artist)
              .fontSize(12)
              .fontColor('#8c96a8')
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)
          Button(this.selectedFavorite ? '♥' : '♡')
            .fontSize(16)
            .width(38)
            .height(30)
            .backgroundColor(this.selectedFavorite ? '#3a1f28' : '#1b2230')
            .fontColor(this.selectedFavorite ? '#ff9ab0' : '#9aa5b8')
            .margin({ right: 8 })
            .onClick(() => {
              this.toggleFavorite(song);
            })
          Button('取消')
            .fontSize(12)
            .height(30)
            .backgroundColor('#1b2230')
            .fontColor('#9aa5b8')
            .onClick(() => {
              this.showSheet = false;
            })
        }
        .width('100%')

        Text(`${song.pack} · BPM ${song.bpm.toFixed(0)}`)
          .fontSize(11)
          .fontColor('#6e788b')
          .width('100%')

        ForEach(song.diffs, (d: DiffInfo) => {
          Row({ space: 12 }) {
            Text(this.diffLabel(d.rc))
              .fontSize(15)
              .fontWeight(FontWeight.Bold)
              .fontColor('#ffffff')
              .width(46)
            Text(d.rating)
              .fontSize(20)
              .fontWeight(FontWeight.Bold)
              .fontColor('#f2f6ff')
              .width(44)
            Column({ space: 2 }) {
              Text(d.designer.length > 0 ? d.designer : '—')
                .fontSize(12)
                .fontColor('#b8c2d2')
                .maxLines(1)
              Text(d.bestScore > 0 ? `BEST ${this.formatScore(d.bestScore)}` : '未游玩')
                .fontSize(10)
                .fontColor(d.bestScore > 0 ? '#e7c66a' : '#5f6e84')
              Text(song.hasAudio ? '点击开始' : '音频未导入')
                .fontSize(10)
                .fontColor(song.hasAudio ? '#6fc9a8' : '#ffb86b')
            }
            .alignItems(HorizontalAlign.Start)
            .layoutWeight(1)
            Text('▶')
              .fontSize(14)
              .fontColor('#dce8ff')
          }
          .width('100%')
          .padding({ left: 14, right: 14, top: 12, bottom: 12 })
          .borderRadius(10)
          .backgroundColor('#33' + this.diffColor(d.rc).substring(1))
          .opacity(song.hasAudio ? 1 : 0.5)
          .onClick(() => {
            if (song.hasAudio) {
              this.showSheet = false;
              this.onStart(song, d);
            } else {
              this.status = '该曲音频未导入，请先导入曲库';
            }
          })
        }, (d: DiffInfo) => `${song.id}-sheet-${d.rc}`)
      }
      .width('100%')
      .padding({ left: 18, right: 18, top: 18, bottom: 28 })
      .backgroundColor('#111722')
      .borderRadius({ topLeft: 18, topRight: 18 })
      .onClick(() => {
      })
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#99000000')
  }

  build() {
    Stack() {
      Column() {
        Row() {
          Column({ space: 2 }) {
            Text('ARCÆA // HAP')
              .fontSize(22)
              .fontWeight(FontWeight.Bold)
              .fontColor('#f4f7ff')
            Text(`${this.filtered.length} 首曲目 · 本地音频 ${this.available}`)
              .fontSize(11)
              .fontColor('#7d8799')
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)
          Row({ space: 8 }) {
            Button('导入曲库')
              .fontSize(12)
              .height(32)
              .backgroundColor('#233556')
              .fontColor('#d8e8ff')
              .onClick(() => {
                this.hideKeyboard();
                this.importLibrary();
              })
            Button('刷新')
              .fontSize(12)
              .height(32)
              .backgroundColor('#1a2130')
              .fontColor('#9fb0c8')
              .onClick(() => {
                this.refreshAudio();
              })
          }
        }
        .width('100%')
        .padding({ left: 16, right: 16, top: 16 })

        TextInput({ placeholder: '搜索曲名 / 曲师 / ID', text: this.query })
          .height(40)
          .fontSize(14)
          .fontColor('#eef3fb')
          .placeholderColor('#5f6a7d')
          .backgroundColor('#141a25')
          .borderRadius(10)
          .margin({ left: 16, right: 16, top: 12 })
          .onChange((value: string) => {
            this.query = value;
            this.applyFilter();
          })

        Scroll() {
          Row({ space: 8 }) {
            this.PackChip()
            this.FavoriteChip()
            this.SortChip()
            this.FilterChip('全部', -1)
            this.FilterChip('PST', 0)
            this.FilterChip('PRS', 1)
            this.FilterChip('FTR', 2)
            this.FilterChip('BYD', 3)
            this.FilterChip('ETR', 4)
          }
          .padding({ left: 16, right: 16 })
        }
        .scrollable(ScrollDirection.Horizontal)
        .scrollBar(BarState.Off)
        .width('100%')
        .margin({ top: 10 })

        if (this.status.length > 0) {
          Text(this.status)
            .fontSize(11)
            .fontColor('#9fb6d8')
            .width('100%')
            .padding({ left: 16, right: 16, top: 6 })
            .maxLines(2)
        }

        if (this.loading) {
          Column({ space: 10 }) {
            LoadingProgress().width(36).height(36).color('#8ab4ff')
            Text('正在加载曲库…').fontSize(13).fontColor('#9aa6bb')
          }
          .width('100%')
          .layoutWeight(1)
          .justifyContent(FlexAlign.Center)
        } else {
          Grid() {
            LazyForEach(this.data, (song: SongInfo) => {
              GridItem() {
                this.SongCard(song)
              }
            }, (song: SongInfo) => song.id)
          }
          .columnsTemplate('1fr 1fr')
          .columnsGap(12)
          .rowsGap(16)
          .padding({ left: 16, right: 16, top: 12, bottom: 28 })
          .layoutWeight(1)
          .scrollBar(BarState.Off)
        }
      }
      .width('100%')
      .height('100%')

      if (this.showSheet) {
        this.DiffSheet(this.selected)
      }

      if (this.showPacks) {
        this.PackSheet()
      }

      if (this.importing) {
        Column({ space: 12 }) {
          LoadingProgress().width(44).height(44).color('#9ec4ff')
          Text('正在导入曲库音频')
            .fontSize(15)
            .fontColor('#eef4ff')
          Text(this.status.length > 0 ? this.status : '正在处理压缩包…')
            .fontSize(12)
            .fontColor('#8fa0ba')
            .textAlign(TextAlign.Center)
            .padding({ left: 28, right: 28 })
          Text('需要本地导入 ZIP，请保持前台并耐心等待')
            .fontSize(11)
            .fontColor('#66748a')
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor('#d0000000')
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#07080d')
  }
}
