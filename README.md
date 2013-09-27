tagenya
=======

なんでも実況V　多元配信ツール

gstreamer1.0で作成
注：gstreamer0.10から移殖したので調整はこれからよ  

[![Bitdeli Badge](https://d2weczhvl823v0.cloudfront.net/kikakubu-ksg/tagenya/trend.png)](https://bitdeli.com/free "Bitdeli Badge")


##使い方  
gstreamer1.2.0ランタイムを落としてインストールする（必須）
windows版は  
http://gstreamer.freedesktop.org/data/pkg/windows/1.2.0/  
のgstreamer-1.0-x86-1.2.0.msi  

インストールしたファイルにパスを通す  
デフォルトの場合、  
PATHにC:\gstreamer\1.0\x86\binを追加  
GST_PLUGIN_PATHを作成してC:\gstreamer\1.0\x86を登録  

#簡単な説明  
鏡置き場にベースポート（デフォルトは47000）から１ずつ足して取得する元数のポートを開けてListenしておく  
９元鏡の場合は47000から47008まで使う  
多元舎実行するとポート監視を開始するので、鏡置き場に接続があったら自動的にex側の配信に出力される  
ex側のデフォルト出力ポートは48000  


以上。

##オプション  
用法:  
  tagenya.exe [オプション...] - Live Multi-Stream Tiling Encoder 多元舎                         
                                                                                             
ヘルプのオプション:                                                                          
  -h, --help                        ヘルプのオプションを表示する                             
  --help-all                        ヘルプのオプションをすべて表示する                       
  --help-gst                        Show GStreamer Options                                   
                                                                                             
アプリケーションのオプション:                                                                
  -S, --silent                      no status information output                             
  -P, --playback                    playback tiled stream                                    
  -V, --version                     show version                                             
  -D, --debug                       debug mode                                               
  -l, --latency                     set latency in nanosecound (default: 20000000000)             
  --video-size                      set each video size (WWWxHHH)                            
  --canvas-size                     set canvas size (WWWxHHH)                                
  --canvas-width                    set number of horizontal tiles                           
  -n, --num                         set how much stream we will accept                       
  --polling                         set polling interval                                     
  -p, --base-port                   set first number of import port                          
  -e, --ex-port                     set the number of export port                            
  -b, --bitrate                     set bit rate (bit per second)                            
  -f, --framerate                   set integer frame rate (fps)                             
  -i, --image                       set canvas image filepath (jpg only)                     



