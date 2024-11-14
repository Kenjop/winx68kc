# WinX68k Compact
SHARP X68000 emulator ソースコードドキュメント / (c) 2024- Kengo Takagi (Kenjo)

## 1. はじめに

　このプログラムは、拙作「WinX68k（けろぴー）」をベースに全般的に再コーディングしたものです。

  - サポートしている機能自体は旧 WinX68k に比べて縮小気味です（ゆえに「Compact」）
  - X68000 エミュレーションコア部分は、アセンブラコードなどの機種依存性は極力排除し移植しやすさを意識した構造となっています。結果として処理速度はそんなに高くないです
  - 描画のエミュレーション精度などは旧 WinX68k よりマシだと思います。が特段高いわけでもないので、こだわりたい方は PI. 氏、および GIMONS 氏作成の「XM6 TypeG」の利用をお勧めします

## 2. コンパイル方法

  - win32/winx68kc.sln からコンパイルしてください。Visual Studio 2017 のプロジェクトになります。
  - DirectX SDKがインストールされている必要があります。手元では Feb.2010 で動作確認しています。

## 3. ソース構成について

  - device/ ... fmgen/MUSASHI などの、このプロジェクト外開発のコードです
  - system/ ... エミュレーションコア用の汎用基幹システムです
  - win32/  ... Windows用の上位層（UI層）コードです
  - x68000/ ... X68000エミュレータのコアコードです