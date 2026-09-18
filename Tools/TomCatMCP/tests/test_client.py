import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from client import Client, AutomationError


class SimulatedEditor(Client):
    def __init__(self):
        super().__init__(project=__file__, port=8091, token="test-token-0123456789")
        self.writes = []
        self.session_value = "original"
        self.fail_next = False

    def _send(self, path, payload=None):
        if path == "/health":
            return {"ok": True, "data": {"project": self.project, "session_id": self.session_value,
                                         "scene_version": "1:2", "protocol_version": 1}}
        self.writes.append(json.dumps(payload, sort_keys=True))
        if self.fail_next:
            self.fail_next = False
            raise AutomationError("OUTCOME_UNKNOWN", "simulated lost reply")
        return {"ok": True, "data": {"id": "18446744073709551615"}}


class ClientTests(unittest.TestCase):
    def test_ambiguous_write_is_not_automatically_replayed(self):
        client = SimulatedEditor()
        client.fail_next = True
        failed = client.call("entity_create", {"name": "方块"})
        self.assertEqual(len(client.writes), 1)
        retry = client.call("entity_create", {"name": "方块", "request_id": failed["error"]["request_id"]})
        self.assertTrue(retry["ok"])
        self.assertEqual(client.writes[0], client.writes[1])
        self.assertIsInstance(retry["data"]["id"], str)

    def test_retry_does_not_cross_editor_restart(self):
        client = SimulatedEditor()
        result = client.call("entity_create", {"name": "test"})
        client.session_value = "restarted"
        failed = client.call("entity_create", {"name": "test", "request_id": result["request_id"]})
        self.assertEqual(failed["error"]["code"], "SESSION_MISMATCH")
        self.assertEqual(len(client.writes), 1)

    def test_retry_cannot_change_arguments(self):
        client = SimulatedEditor()
        result = client.call("entity_create", {"name": "test"})
        failed = client.call("entity_create", {"name": "other", "request_id": result["request_id"]})
        self.assertEqual(failed["error"]["code"], "REQUEST_ID_REUSED")
        self.assertEqual(len(client.writes), 1)

    def test_unknown_retry_is_never_a_fresh_write(self):
        client = SimulatedEditor()
        failed = client.call("entity_create", {"name": "test", "request_id": "missing"})
        self.assertEqual(failed["error"]["code"], "REQUEST_EXPIRED")
        self.assertFalse(client.writes)

    def test_optimistic_precondition_is_preserved(self):
        client = SimulatedEditor()
        client.call("entity_create", {"name": "test", "scene_version": "old:1"})
        self.assertEqual(json.loads(client.writes[0])["scene_version"], "old:1")

    def test_invalid_keys_never_reach_editor(self):
        client = SimulatedEditor()
        self.assertFalse(client.call("entity_create", {"typo": "test"})["ok"])
        self.assertFalse(client.writes)

    def test_non_object_arguments_are_rejected(self):
        client = SimulatedEditor()
        self.assertEqual(client.call("entity_create", ["bad"])["error"]["code"], "INVALID_ARGUMENT")
        self.assertFalse(client.writes)

    def test_nonfinite_json_is_rejected_before_network(self):
        client = Client(project=__file__, token="test-token-0123456789")
        with self.assertRaises(AutomationError) as caught:
            client._send("/call", {"value": float("nan")})
        self.assertEqual(caught.exception.code, "INVALID_ARGUMENT")


if __name__ == "__main__":
    unittest.main()
