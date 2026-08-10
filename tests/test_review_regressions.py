from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class ReviewRegressionTests(unittest.TestCase):
    def test_dht11_uses_measured_high_pulse_width(self):
        text = source("firmware_f103/BSP/bsp_dht11.c")
        self.assertIn("high_width >= 45u", text)
        self.assertIn("BSP_Time_Micros16", text)
        self.assertNotIn("BSP_Time_DelayUs(35u)", text)

    def test_alarm_reset_has_silence_and_rearm_hysteresis(self):
        text = source("firmware_f103/User/main.c")
        self.assertIn("s_alarm_silenced = 1u", text)
        self.assertIn("EG_ALARM_HYSTERESIS_CENTI", text)
        self.assertIn("send_frame(EG_MSG_HEARTBEAT, NULL, 0u)", text)

    def test_threshold_is_committed_only_after_ack(self):
        text = source("mp157_hmi/mainwindow.cpp")
        apply_block = text[text.index("void MainWindow::applyThreshold"):
                           text.index("void MainWindow::resetAlarm")]
        ack_block = text[text.index("void MainWindow::onCommandAck"):
                         text.index("void MainWindow::refreshHistory")]
        self.assertEqual(apply_block.count("m_threshold = m_thresholdSpin->value()"), 1)
        self.assertIn("m_threshold = m_pendingThreshold", ack_block)
        self.assertIn("m_thresholdAckTimer.start()", apply_block)

    def test_invalid_sensor_fields_are_not_displayed_as_values(self):
        text = source("mp157_hmi/mainwindow.cpp")
        self.assertIn("EG_SENSOR_VALID_TEMPERATURE_HUMIDITY", text)
        self.assertIn("QStringLiteral(\"--\")", text)
        self.assertIn("温湿度数据无效", text)

    def test_trend_uses_current_sample_count_for_x_axis(self):
        text = source("mp157_hmi/trendwidget.cpp")
        self.assertIn("values.size() - 1", text)
        self.assertNotIn("m_capacity - 1", text)

    def test_mqtt_uses_one_line_mode_process(self):
        text = source("imx6ull_aggregator/mqttforwarder.cpp")
        self.assertIn("QStringLiteral(\"-l\")", text)
        self.assertNotIn("QStringLiteral(\"-m\")", text)
        self.assertIn("QProcess::errorOccurred", text)
        self.assertIn("queue full", text)

    def test_tcp_oversize_path_sends_negative_ack(self):
        text = source("imx6ull_aggregator/aggregatorserver.cpp")
        self.assertIn("MAX_JSON_LINE_BYTES", text)
        self.assertIn("unterminated JSON line too long", text)
        self.assertIn("sendAck(client, 0, false", text)

    def test_http_has_cors_and_options(self):
        text = source("imx6ull_aggregator/statushttpserver.cpp")
        self.assertIn("Access-Control-Allow-Origin: *", text)
        self.assertIn("OPTIONS", text)
        self.assertIn("204 No Content", text)

    def test_installers_preserve_existing_configuration(self):
        for relative in ("deploy/install_mp157.sh", "deploy/install_imx6ull.sh"):
            text = source(relative)
            self.assertIn("--force-config", text)
            self.assertIn("preserved existing", text)

    def test_extended_frame_builder_reports_error_categories(self):
        header = source("common/edge_protocol.h")
        implementation = source("common/edge_protocol.c")
        self.assertIn("EG_BUILD_PAYLOAD_TOO_LARGE", header)
        self.assertIn("EG_BUILD_OUTPUT_TOO_SMALL", header)
        self.assertIn("eg_build_frame_ex", implementation)


if __name__ == "__main__":
    unittest.main(verbosity=2)
