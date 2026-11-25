export class PlayResult {
  score: number = 0;
  pure: number = 0;
  far: number = 0;
  lost: number = 0;
  maxCombo: number = 0;
  total: number = 0;
  duration: number = 0;
  newBest: boolean = false;
  trackLost: boolean = false;
  gauge: number = 0; // 终曲 Recollection Rate（Normal/Easy <70 即 Track Lost）

  accuracy(): number {
    if (this.total <= 0) return 0;
    return (this.pure + this.far * 0.5) * 100 / this.total;
  }

  grade(): string {
    if (this.trackLost) return 'TRACK LOST';
    if (this.total > 0 && this.lost === 0 && this.far === 0) return 'PURE MEMORY';
    if (this.total > 0 && this.lost === 0) return 'FULL RECALL';
    return 'TRACK COMPLETE';
  }

  gradeColor(): string {
    if (this.trackLost) return '#ff7a7a';
    if (this.total > 0 && this.lost === 0 && this.far === 0) return '#fff2a8';
    if (this.total > 0 && this.lost === 0) return '#9fe8ff';
    return '#b8c0cc';
  }
}
