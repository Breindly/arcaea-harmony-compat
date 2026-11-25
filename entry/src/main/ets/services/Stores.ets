import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';

export class ScoreStore {
  private store: preferences.Preferences;

  constructor(ctx: common.UIAbilityContext) {
    const opts: preferences.Options = { name: 'arc_scores' };
    this.store = preferences.getPreferencesSync(ctx, opts);
  }

  private key(songId: string, rc: number): string {
    return `${songId}#${rc}`;
  }

  get(songId: string, rc: number): number {
    const v: preferences.ValueType = this.store.getSync(this.key(songId, rc), 0);
    return typeof v === 'number' ? v : 0;
  }

  put(songId: string, rc: number, score: number): void {
    if (score <= this.get(songId, rc)) return;
    this.store.putSync(this.key(songId, rc), score);
    this.store.flush();
  }
}

export class SettingsStore {
  private store: preferences.Preferences;

  constructor(ctx: common.UIAbilityContext) {
    const opts: preferences.Options = { name: 'arc_settings' };
    this.store = preferences.getPreferencesSync(ctx, opts);
  }

  getOffset(): number {
    const v: preferences.ValueType = this.store.getSync('audio_offset', 0);
    return typeof v === 'number' ? v : 0;
  }

  setOffset(ms: number): void {
    this.store.putSync('audio_offset', ms);
    this.store.flush();
  }

  getSpeed(): number {
    const v: preferences.ValueType = this.store.getSync('note_speed', 3);
    return typeof v === 'number' ? v : 3;
  }

  setSpeed(speed: number): void {
    this.store.putSync('note_speed', speed);
    this.store.flush();
  }

  /** 血条模式：0=Normal 1=Easy 2=Hard */
  getGaugeKind(): number {
    const v: preferences.ValueType = this.store.getSync('gauge_kind', 0);
    if (typeof v !== 'number') return 0;
    return v >= 0 && v <= 2 ? Math.floor(v) : 0;
  }

  setGaugeKind(kind: number): void {
    this.store.putSync('gauge_kind', kind);
    this.store.flush();
  }

  getFavorites(): string {
    const v: preferences.ValueType = this.store.getSync('favorites', '');
    return typeof v === 'string' ? v : '';
  }

  setFavorites(csv: string): void {
    this.store.putSync('favorites', csv);
    this.store.flush();
  }
}
