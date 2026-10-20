"""旧发行清理的选择与身份屏障。 / Selection and identity barriers for release retirement."""
import importlib.util
import pathlib
import sys
import unittest
from unittest.mock import Mock, MagicMock
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))
import retire_gitee_releases as retire

class Retention(unittest.TestCase):
    def test_only_older_canonical_non_drafts(self):
        tags = ["v0.9.21", "v1.0.0", "v1.0.1", "nightly", "v01.0.0", "v0.8.0"]
        rows = [{"id": n+1, "tag_name": t, "draft": t == "v0.8.0"} for n, t in enumerate(tags)]
        self.assertEqual(retire.candidates(rows,"v1.0.0"), [{"id":1,"tag_name":"v0.9.21"}])
    def test_invalid_snapshot_fails_before_plan(self):
        for rows in (None, [{"id":True,"tag_name":"v0.8.0"}], [{"id":1,"tag_name":"v0.8.0"}]*2, [{"id":-1,"tag_name":"v0.8.0"}]):
            with self.assertRaises(ValueError):retire.candidates(rows,"v1.0.0")
    def test_invalid_keeper(self):
        for tag in (None,"../v1.0.0","v1.0","v01.0.0","v1.0.0;echo x"):
            with self.assertRaises(ValueError):retire.candidates([],tag)
    def test_pagination_completed_before_plan(self):
        api=Mock();api.request.side_effect=[[{"id":i+1,"tag_name":f"v0.0.{i}"} for i in range(100)], [{"id":101,"tag_name":"v1.0.0"}]]
        self.assertEqual(len(retire.inventory(api)),101);self.assertEqual(api.request.call_count,2)
    def test_changed_identity_never_deletes(self):
        api=Mock();api.request.return_value={"id":2,"tag_name":"v0.9.0"}
        with self.assertRaises(ValueError):retire.delete_release(api,{"id":1,"tag_name":"v0.9.0"})
        api.opener.open.assert_not_called()
    def test_already_absent_is_idempotent(self):
        api=Mock();api.request.return_value=None
        self.assertEqual(retire.delete_release(api,{"id":1,"tag_name":"v0.9.0"}),"already-absent")
        api.opener.open.assert_not_called()
    def test_delete_is_read_back_and_never_targets_tags(self):
        api=MagicMock(origin="https://gitee.com/api/v5",token="test");api.request.side_effect=[{"id":1,"tag_name":"v0.9.0"},None]
        api.opener.open.return_value.__enter__.return_value.status=204
        self.assertEqual(retire.delete_release(api,{"id":1,"tag_name":"v0.9.0"}),"deleted")
        req=api.opener.open.call_args.args[0];self.assertEqual(req.method,"DELETE");self.assertTrue(req.full_url.endswith("/releases/1"))
    def test_readback_failure_stops(self):
        api=MagicMock(origin="https://gitee.com/api/v5",token="test");api.request.return_value={"id":1,"tag_name":"v0.9.0"};api.opener.open.return_value.__enter__.return_value.status=204
        with self.assertRaises(ValueError):retire.delete_release(api,{"id":1,"tag_name":"v0.9.0"})

if __name__ == "__main__":unittest.main()
