import { common } from '@kit.AbilityKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { zlib } from '@kit.BasicServicesKit';
import { SongInfo, DiffInfo } from '../models/Library';

const DEMO_SONGS: string[] = ['abstrusedilemma', 'bluecomet', 'defection', 'theformula'];

export class SongFiles {
  private ctx: common.UIAbilityContext;
  private root: string = '';

  constructor(ctx: common.UIAbilityContext) {
    this.ctx = ctx;
    this.root = this.detectRoot();
  }

  private exists(path: string): boolean {
    try {
      return fs.accessSync(path);
    } catch (e) {
      return false;
    }
  }

  private countSongs(dir: string): number {
    let count: number = 0;
    try {
      const names: string[] = fs.listFileSync(dir);
      for (const name of names) {
        if (this.exists(dir + '/' + name + '/base.ogg')) count++;
      }
    } catch (e) {
      return 0;
    }
    return count;
  }

  private detectRoot(): string {
    const primary: string = this.ctx.filesDir + '/songs';
    const nested: string = primary + '/songs';
    const a: number = this.countSongs(primary);
    const b: number = this.countSongs(nested);
    if (b > a) return nested;
    return primary;
  }

  songsDir(): string {
    return this.root;
  }

  hasAudio(song: SongInfo): boolean {
    const dir: string = this.songsDir() + '/' + song.id;
    for (const name of song.audio) {
      if (this.exists(dir + '/' + name)) return true;
    }
    return false;
  }

  scan(songs: SongInfo[]): number {
    let count: number = 0;
    for (const song of songs) {
      song.hasAudio = this.hasAudio(song);
      if (song.hasAudio) count++;
    }
    return count;
  }

  audioPath(song: SongInfo, diff: DiffInfo): string {
    const dir: string = this.songsDir() + '/' + song.id;
    if (diff.rc === 3) {
      const beyond: string = dir + '/3.ogg';
      if (this.exists(beyond)) return beyond;
    }
    const base: string = dir + '/base.ogg';
    if (this.exists(base)) return base;
    for (const name of song.audio) {
      const p: string = dir + '/' + name;
      if (this.exists(p)) return p;
    }
    return '';
  }

  async ensureDemo(songs: SongInfo[]): Promise<void> {
    const byId: Map<string, SongInfo> = new Map<string, SongInfo>();
    for (const s of songs) byId.set(s.id, s);
    for (const id of DEMO_SONGS) {
      const song: SongInfo | undefined = byId.get(id);
      if (song === undefined || this.hasAudio(song)) continue;
      try {
        const bytes: Uint8Array =
          await this.ctx.resourceManager.getRawFileContent(`demo/${id}/base.ogg`);
        const dir: string = this.songsDir() + '/' + id;
        fs.mkdirSync(dir, true);
        const file = fs.openSync(dir + '/base.ogg',
          fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
        fs.writeSync(file.fd, bytes.buffer);
        fs.closeSync(file);
      } catch (e) {
        console.warn(`demo audio ${id} unavailable`);
      }
    }
  }

  // 选择 zip（内含 <songId>/base.ogg 或 songs/<songId>/base.ogg），
  // 复制到 cache 后由系统 zlib 解压到沙箱。
  async importZip(uri: string, onStatus: (text: string) => void): Promise<void> {
    const zipPath: string = this.ctx.cacheDir + '/compat_import.zip';
    try {
      if (this.exists(zipPath)) fs.unlinkSync(zipPath);
    } catch (e) {
    }
    onStatus('正在复制压缩包到应用沙箱…');
    const srcFile = fs.openSync(uri, fs.OpenMode.READ_ONLY);
    try {
      await fs.copyFile(srcFile.fd, zipPath);
    } finally {
      fs.closeSync(srcFile);
    }
    onStatus('正在解压音频，首次约 1.8GB，请耐心等待…');
    await zlib.decompressFile(zipPath, this.songsDir());
    try {
      fs.unlinkSync(zipPath);
    } catch (e) {
    }
    this.root = this.detectRoot();
  }

  clearCacheZip(): void {
    try {
      const zipPath: string = this.ctx.cacheDir + '/compat_import.zip';
      if (this.exists(zipPath)) fs.unlinkSync(zipPath);
    } catch (e) {
    }
  }
}
