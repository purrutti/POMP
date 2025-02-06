using System;
using System.IO;
using System.Net;
using System.Windows;
using Fleck;

namespace SuperviPOMP
{
    public partial class MainWindow : Window
    {
        private WebSocketServer _webSocketServer;
        private HttpListener _httpListener;
        private string _webRoot = "wwwroot"; // Répertoire où sont stockés les fichiers HTML

        public MainWindow()
        {
            InitializeComponent();
        }

        private void StartServer_Click(object sender, RoutedEventArgs e)
        {
            StartWebSocketServer();
            StartHttpServer();
            Log("Servers started.");
        }

        private void StopServer_Click(object sender, RoutedEventArgs e)
        {
            StopWebSocketServer();
            StopHttpServer();
            Log("Servers stopped.");
        }

        private void StartWebSocketServer()
        {
            _webSocketServer = new WebSocketServer("ws://0.0.0.0:8181");
            _webSocketServer.Start(socket =>
            {
                socket.OnOpen = () => Log("WebSocket connection opened.");
                socket.OnClose = () => Log("WebSocket connection closed.");
                socket.OnMessage = message => Log($"WebSocket message received: {message}");
            });
        }

        private void StopWebSocketServer()
        {
            _webSocketServer?.Dispose();
            _webSocketServer = null;
        }

        private void StartHttpServer()
        {
            _httpListener = new HttpListener();
            _httpListener.Prefixes.Add("http://*:8080/");
            _httpListener.Start();
            _httpListener.BeginGetContext(OnHttpRequest, null);
        }

        private void StopHttpServer()
        {
            _httpListener?.Stop();
            _httpListener?.Close();
            _httpListener = null;
        }

        private void OnHttpRequest(IAsyncResult result)
        {
            if (_httpListener == null || !_httpListener.IsListening)
                return;

            var context = _httpListener.EndGetContext(result);
            _httpListener.BeginGetContext(OnHttpRequest, null);

            string filePath = Path.Combine(_webRoot, context.Request.Url.LocalPath.TrimStart('/'));

            if (File.Exists(filePath))
            {
                byte[] buffer = File.ReadAllBytes(filePath);
                context.Response.ContentLength64 = buffer.Length;
                using (var output = context.Response.OutputStream)
                {
                    output.Write(buffer, 0, buffer.Length);
                }
            }
            else
            {
                context.Response.StatusCode = (int)HttpStatusCode.NotFound;
                using (var output = new StreamWriter(context.Response.OutputStream))
                {
                    output.Write("404 Not Found");
                }
            }
        }

        private void Log(string message)
        {
            Dispatcher.Invoke(() =>
            {
                LogTextBox.AppendText($"{DateTime.Now}: {message}\n");
            });
        }
    }
}
