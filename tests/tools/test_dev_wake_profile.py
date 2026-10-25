"""验证唤醒诊断入口与正式/控制台配置隔离。 / Verify isolation of wake diagnostics from production and console profiles."""
import importlib.util
import pathlib
import sys
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('pn_dev', ROOT/'tools/dev.py')
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)


class WakeProfile(unittest.TestCase):
    def commands(self, action):
        with patch.object(dev, 'prepare_console'), patch.object(sys, 'argv', ['dev.py', action]), patch.object(dev, 'run', return_value=0) as run:
            self.assertEqual(dev.main(), 0)
            return [call.args[0] for call in run.call_args_list]

    def test_isolated_commands(self):
        for action, folder, overlay in [('firmware-board', 'build-board', 'sdkconfig.defaults'),
                                         ('firmware-diag', 'build-diag', 'sdkconfig.defaults;sdkconfig.diag'),
                                         ('firmware-wake-diag', 'build-wake-diag', 'sdkconfig.defaults;sdkconfig.wake-diag')]:
            commands = self.commands(action)
            self.assertEqual(len(commands), 3 if action == 'firmware-wake-diag' else 2)
            for command in commands:
                self.assertIn(folder, command)
                self.assertIn('-DSDKCONFIG=/work/'+folder+'/sdkconfig', command)
                self.assertIn('-DSDKCONFIG_DEFAULTS='+overlay, command)
                self.assertEqual('-DPN_DIAG_IMAGE=ON' in command, action != 'firmware-board')
            self.assertEqual([command[-1] for command in commands], ['reconfigure', 'build', 'size'] if action == 'firmware-wake-diag' else ['build', 'size'])

    def test_real_sleep_without_console(self):
        config = (ROOT/'sdkconfig.wake-diag').read_text(encoding='utf-8')
        for flag in ('PN_TEST_CONSOLE', 'PN_PROFILE', 'PN_DIAG_NO_AUTO_LOCK', 'PN_DIAG_NO_LOCK_SLEEP'):
            self.assertIn('CONFIG_'+flag+'=n', config)
        self.assertIn('CONFIG_LOG_DEFAULT_LEVEL_INFO=y', config)
        self.assertIn('CONFIG_PN_WAKE_TRACE=y', config)

    def test_trace_cannot_be_packaged_as_product(self):
        spec = importlib.util.spec_from_file_location('pn_package', ROOT/'tools/package_upgrade.py')
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        config = 'CONFIG_SPIRAM_SPEED_120M=y\nCONFIG_ESP_PHY_ENABLE_USB=y\nCONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y\n'
        package.validate_build_config(config)
        for flag in ('PN_WAKE_TRACE', 'PN_DIAG_NO_AUTO_LOCK', 'PN_DIAG_NO_LOCK_SLEEP'):
            with self.assertRaises(ValueError):
                package.validate_build_config(config+'CONFIG_'+flag+'=y\n')


if __name__ == '__main__':
    unittest.main()
