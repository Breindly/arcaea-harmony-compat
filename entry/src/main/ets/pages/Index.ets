import { SongInfo, DiffInfo } from '../models/Library';
import { PlayResult } from '../models/PlayResult';
import { SongSelect } from '../components/SongSelect';
import { PlayScreen } from '../components/PlayScreen';
import { ResultScreen } from '../components/ResultScreen';

@Entry
@Component
struct Index {
  @State screen: number = 0; // 0=曲库 1=游玩 2=结算
  @State pickedSong: SongInfo = new SongInfo();
  @State pickedDiff: DiffInfo = new DiffInfo();
  @State result: PlayResult = new PlayResult();

  onPageShow(): void {
    AppStorage.setOrCreate('arc.appHidden', false);
  }

  onPageHide(): void {
    AppStorage.setOrCreate('arc.appHidden', true);
  }

  build() {
    Stack() {
      if (this.screen === 0) {
        SongSelect({
          onStart: (song: SongInfo, diff: DiffInfo) => {
            this.pickedSong = song;
            this.pickedDiff = diff;
            this.screen = 1;
          }
        })
      } else if (this.screen === 1) {
        PlayScreen({
          song: this.pickedSong,
          diff: this.pickedDiff,
          onExit: () => {
            this.screen = 0;
          },
          onFinish: (result: PlayResult) => {
            this.result = result;
            this.screen = 2;
          }
        })
      } else {
        ResultScreen({
          song: this.pickedSong,
          diff: this.pickedDiff,
          result: this.result,
          onRetry: () => {
            this.screen = 1;
          },
          onBack: () => {
            this.screen = 0;
          }
        })
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#07080d')
  }
}
