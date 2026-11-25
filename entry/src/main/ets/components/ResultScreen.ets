import { SongInfo, DiffInfo } from '../models/Library';
import { PlayResult } from '../models/PlayResult';

@Component
export struct ResultScreen {
  song: SongInfo = new SongInfo();
  diff: DiffInfo = new DiffInfo();
  result: PlayResult = new PlayResult();
  onRetry: () => void = () => {};
  onBack: () => void = () => {};

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

  build() {
    Column() {
      Row() {
        Text('RESULT')
          .fontSize(11)
          .fontColor('#76839a')
          .letterSpacing(3)
        Blank()
        Text(`${this.diffLabel()} ${this.diff.rating}`)
          .fontSize(11)
          .fontColor(this.diffColor())
          .padding({ left: 8, right: 8, top: 3, bottom: 3 })
          .backgroundColor('#22' + this.diffColor().substring(1))
          .borderRadius(5)
      }
      .width('100%')
      .padding({ left: 22, right: 22, top: 24 })

      Column({ space: 6 }) {
        Image($rawfile(`library/jackets/${this.song.jacket}`))
          .width(132)
          .height(132)
          .objectFit(ImageFit.Cover)
          .borderRadius(12)
          .margin({ top: 22 })
        Text(this.song.title)
          .fontSize(20)
          .fontWeight(FontWeight.Bold)
          .fontColor('#f3f7ff')
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
          .padding({ left: 26, right: 26 })
        Text(this.song.artist)
          .fontSize(12)
          .fontColor('#8792a5')
      }
      .width('100%')
      .alignItems(HorizontalAlign.Center)

      Row({ space: 8 }) {
        Text(this.result.grade())
          .fontSize(16)
          .fontWeight(FontWeight.Bold)
          .fontColor(this.result.gradeColor())
        if (this.result.newBest) {
          Text('NEW BEST')
            .fontSize(10)
            .fontWeight(FontWeight.Bold)
            .fontColor('#ffe08a')
            .padding({ left: 7, right: 7, top: 2, bottom: 2 })
            .backgroundColor('#3a2f14')
            .borderRadius(5)
        }
      }
      .margin({ top: 22 })

      Text(this.formatScore(this.result.score))
        .fontSize(44)
        .fontWeight(FontWeight.Bold)
        .fontColor('#f8fbff')
        .margin({ top: 2 })

      Text(`${this.result.accuracy().toFixed(2)}%`)
        .fontSize(14)
        .fontColor('#9fb6d6')
        .margin({ top: 2 })
      Text(`GAUGE ${this.result.gauge}%`)
        .fontSize(11)
        .fontColor(this.result.trackLost ? '#ff8a8a' : '#7d8aa0')
        .margin({ top: 2 })

      Row({ space: 22 }) {
        Column({ space: 2 }) {
          Text(`${this.result.pure}`).fontSize(22).fontColor('#7fd4ff').fontWeight(FontWeight.Bold)
          Text('PURE').fontSize(10).fontColor('#64748b')
        }
        Column({ space: 2 }) {
          Text(`${this.result.far}`).fontSize(22).fontColor('#ffd66b').fontWeight(FontWeight.Bold)
          Text('FAR').fontSize(10).fontColor('#64748b')
        }
        Column({ space: 2 }) {
          Text(`${this.result.lost}`).fontSize(22).fontColor('#ff7a7a').fontWeight(FontWeight.Bold)
          Text('LOST').fontSize(10).fontColor('#64748b')
        }
        Column({ space: 2 }) {
          Text(`${this.result.maxCombo}`).fontSize(22).fontColor('#d9e5f7').fontWeight(FontWeight.Bold)
          Text('MAX COMBO').fontSize(10).fontColor('#64748b')
        }
      }
      .margin({ top: 26 })

      Blank()

      Row({ space: 12 }) {
        Button('再来一次')
          .layoutWeight(1)
          .height(48)
          .fontSize(15)
          .backgroundColor('#2b3f66')
          .fontColor('#eaf2ff')
          .onClick(() => {
            this.onRetry();
          })
        Button('返回曲库')
          .layoutWeight(1)
          .height(48)
          .fontSize(15)
          .backgroundColor('#171e2b')
          .fontColor('#b8c6da')
          .onClick(() => {
            this.onBack();
          })
      }
      .width('100%')
      .padding({ left: 22, right: 22, bottom: 30 })
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#07080d')
  }
}
