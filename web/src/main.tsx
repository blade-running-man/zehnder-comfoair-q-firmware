import { render } from "preact";
import Router from "preact-router";
import { Nav } from "./components/Nav";
import { connectDevice } from "./api/device";
import { ToastContainer } from "./components/Toast";
import { Watermark } from "./components/Watermark";
import { DashboardPage } from "./pages/dashboard";
import { StatePage } from "./pages/state";
import { ProbePage } from "./pages/probe";
import { SystemPage } from "./pages/system";
import { SettingsPage } from "./pages/settings";
import { LogPage } from "./pages/log";
import { UpdatePage } from "./pages/update";
import "./styles/global.css";
import styles from "./main.module.css";

function App() {
  return (
    <div>
      <Nav />
      <main class={styles.main}>
        <Router>
          <DashboardPage path="/" />
          <StatePage path="/diagnostics" />
          <ProbePage path="/capabilities" />
          <SystemPage path="/system" />
          <SettingsPage path="/settings" />
          <LogPage path="/log" />
          <UpdatePage path="/update" />
          <DashboardPage default />
        </Router>
      </main>
      <ToastContainer />
      <Watermark />
    </div>
  );
}

// Opened once, for the life of the page, rather than per screen: the device serves seven
// sockets in total, shared with every ordinary HTTP request (docs/interfaces/websocket.md),
// and a socket per mounted page would spend that budget on navigation. DeviceSocket
// reconnects on its own, so there is nothing to tear down when a route changes.
connectDevice();

render(<App />, document.getElementById("app")!);
