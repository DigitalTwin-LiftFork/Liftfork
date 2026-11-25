import copy
import tempfile
import time
import unittest
from pathlib import Path
from server import Coordinator


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.c = Coordinator(':memory:')
        self.data = dict(session='truck1', ready=False, loaded='',
                         pallets=[dict(id='P-001'), dict(id='P-002')],
                         slots=[dict(id='D-20', occupied=False, pallet=''),
                                dict(id='A-01', occupied=False, pallet='')])
        self.c.sync(copy.deepcopy(self.data))

    def tearDown(self):
        self.c.db.close()

    def job(self):
        return self.c.create(dict(pallet='P-001', destination='D-20'))['id']

    def assign(self):
        job = self.job()
        self.data.update(ready=True, loaded='P-001')
        self.assertEqual(self.c.sync(copy.deepcopy(self.data))['job']['id'], job)
        return job

    def test_duplicate_pallet_and_destination(self):
        self.job()
        for payload in [dict(pallet='P-001', destination='A-01'),
                        dict(pallet='P-002', destination='D-20')]:
            with self.assertRaises(ValueError):
                self.c.create(payload)

    def test_dispatch_requires_correct_load(self):
        self.job()
        self.data.update(ready=True, loaded='P-002')
        self.assertIsNone(self.c.sync(copy.deepcopy(self.data))['job'])

    def test_occupied_destination(self):
        self.data['slots'][0]['occupied'] = True
        self.c.sync(copy.deepcopy(self.data))
        with self.assertRaises(ValueError):
            self.job()

    def test_verified_completion(self):
        job = self.assign()
        self.data.update(ready=False, loaded='', report=dict(id=job, status='completed'))
        self.data['slots'][0].update(occupied=True, pallet='P-001')
        result = self.c.sync(copy.deepcopy(self.data))
        self.assertEqual(result['ack'], job)
        self.assertEqual(self.c.rows()[0]['status'], 'completed')

    def test_false_completion_rejected(self):
        job = self.assign()
        self.data['report'] = dict(id=job, status='completed')
        self.c.sync(copy.deepcopy(self.data))
        self.assertEqual(self.c.rows()[0]['status'], 'failed')

    def test_active_cancel_keeps_reservation_until_ack(self):
        job = self.assign()
        self.c.cancel(job)
        self.assertEqual(self.c.state()['slots'][0]['reserved_by'], job)
        self.assertEqual(self.c.sync(copy.deepcopy(self.data))['job']['cancel_requested'], 1)
        self.data['report'] = dict(id=job, status='cancelled')
        self.c.sync(copy.deepcopy(self.data))
        self.assertIsNone(self.c.state()['slots'][0]['reserved_by'])

    def test_offline_blocks_creation(self):
        self.c.seen = time.time() - 6
        with self.assertRaises(ValueError):
            self.job()

    def test_second_live_session_rejected(self):
        self.data['session'] = 'truck2'
        with self.assertRaises(ValueError):
            self.c.sync(copy.deepcopy(self.data))

    def test_restart_interrupts_active(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory)/'jobs.sqlite')
            first = Coordinator(path)
            first.sync(copy.deepcopy(self.data))
            first.create(dict(pallet='P-001', destination='D-20'))
            first.sync(dict(self.data, loaded='P-001', ready=True))
            first.db.close()
            second = Coordinator(path)
            self.assertEqual(second.rows()[0]['status'], 'interrupted')
            second.db.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
