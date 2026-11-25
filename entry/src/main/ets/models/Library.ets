import { common } from '@kit.AbilityKit';
import { util } from '@kit.ArkTS';

export interface RawDiff {
  rc: number;
  rating: string;
  designer: string;
}

export interface RawSong {
  id: string;
  title: string;
  titleZh: string;
  artist: string;
  bpm: number;
  set: string;
  pack: string;
  side: number;
  jacket: string;
  audio: string[];
  difficulties: RawDiff[];
}

export interface RawLibrary {
  version: number;
  songs: RawSong[];
}

export class PackInfo {
  name: string = '';
  count: number = 0;

  constructor(name: string = '', count: number = 0) {
    this.name = name;
    this.count = count;
  }
}

export class DiffInfo {
  rc: number = 0;
  rating: string = '';
  designer: string = '';
  bestScore: number = 0;

  constructor(rc: number = 0, rating: string = '', designer: string = '') {
    this.rc = rc;
    this.rating = rating;
    this.designer = designer;
  }
}

export class SongInfo {
  id: string = '';
  title: string = '';
  titleZh: string = '';
  artist: string = '';
  bpm: number = 0;
  set: string = '';
  pack: string = '';
  jacket: string = '_default.jpg';
  audio: string[] = [];
  diffs: DiffInfo[] = [];
  hasAudio: boolean = false;
  favorite: boolean = false;

  static fromRaw(raw: RawSong): SongInfo {
    const s: SongInfo = new SongInfo();
    s.id = raw.id;
    s.title = raw.title;
    s.titleZh = raw.titleZh;
    s.artist = raw.artist;
    s.bpm = raw.bpm;
    s.set = raw.set;
    s.pack = raw.pack;
    s.jacket = raw.jacket;
    s.audio = raw.audio;
    for (const d of raw.difficulties) {
      s.diffs.push(new DiffInfo(d.rc, d.rating, d.designer));
    }
    return s;
  }

  hasDiff(rc: number): boolean {
    for (const d of this.diffs) {
      if (d.rc === rc) return true;
    }
    return false;
  }

  diffAt(rc: number): DiffInfo | null {
    for (const d of this.diffs) {
      if (d.rc === rc) return d;
    }
    return null;
  }

  bestDiffRating(): string {
    let best: string = '';
    for (const d of this.diffs) {
      if (d.rc >= 2) return d.rating;
      best = d.rating;
    }
    return best;
  }
}

export function decodeUtf8(bytes: Uint8Array): string {
  const decoder: util.TextDecoder = util.TextDecoder.create('utf-8');
  return decoder.decodeToString(bytes);
}

export class LibraryStore {
  private ctx: common.UIAbilityContext;

  constructor(ctx: common.UIAbilityContext) {
    this.ctx = ctx;
  }

  async load(): Promise<SongInfo[]> {
    const data: Uint8Array = await this.ctx.resourceManager.getRawFileContent('library/library.json');
    const raw: RawLibrary = JSON.parse(decodeUtf8(data)) as RawLibrary;
    const songs: SongInfo[] = [];
    for (const item of raw.songs) {
      songs.push(SongInfo.fromRaw(item));
    }
    return songs;
  }

  async readChart(songId: string, rc: number): Promise<string> {
    const path: string = `library/charts/${songId}/${rc}.aff`;
    try {
      const data: Uint8Array = this.ctx.resourceManager.getRawFileContentSync(path);
      return decodeUtf8(data);
    } catch (e) {
      console.error(`readChart ${path} failed: ${JSON.stringify(e)}`);
      throw new Error(`rawfile ${path}: ${JSON.stringify(e)}`);
    }
  }
}
