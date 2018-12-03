import find_projection
import unittest

import pyrt.windows.foundation.collections as wfc

class TestCollections(unittest.TestCase):

    def test_stringmap(self):
        m = wfc.StringMap()
        m.Insert("hello", "world")

        self.assertTrue(m.HasKey("hello"))
        self.assertFalse(m.HasKey("world"))
        self.assertEqual(m.Size, 1)
        self.assertEqual(m.Lookup("hello"), "world")


    def test_stringmap_changed_event(self):
        called = {}

        def onMapChanged(sender, args): 
            self.assertEqual(args.CollectionChange, 1)
            self.assertEqual(args.Key, "dr")

            self.assertEqual(sender.Size, 2)
            self.assertTrue(sender.HasKey("dr"))
            self.assertTrue(sender.HasKey("hello"))

            called[args.Key] = True

        m = wfc.StringMap()
        m.Insert("hello", "world")
        token = m.add_MapChanged(onMapChanged)
        m.Insert("dr", "who")
        m.remove_MapChanged(token)
        m.Insert("king", "arthur")


        self.assertTrue(called.get("dr"))
        self.assertFalse(called.get("hello"), False)
        self.assertFalse(called.get("king"), False)

if __name__ == '__main__':
    unittest.main()
