#!/usr/bin/env python3

# lame ass integration test

import requests
import unittest

base = 'http://localhost:10000'

class IntegrationTest(unittest.TestCase):
    def testMain(self):
        resp = requests.get(base + '/hello.html')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')
        self.assertEqual(resp.headers['Content-Type'], 'text/html')

        resp = requests.get(base + '/hello.html?foo=bar&baz=lol?rofl')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')
        self.assertEqual(resp.headers['Content-Type'], 'text/html')

        resp = requests.get(base + '/')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')
        self.assertEqual(resp.headers['Content-Type'], 'text/html')

        resp = requests.get(base + '/test.css')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'css')
        self.assertEqual(resp.headers['Content-Type'], 'text/css')

        resp = requests.get(base + '/noending')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'noending')
        self.assertEqual(resp.headers['Content-Type'], 'text/html')

        resp = requests.get(base + '/foo.unknown')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'foo')
        self.assertEqual(resp.headers['Content-Type'], 'text/html')

        resp = requests.get(base + '/subdir/')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'subdir hello')
        self.assertEqual(resp.headers['Content-Type'], 'text/html')

        sess = requests.session()

        resp = sess.get(base + '/hello.html')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')

        resp = sess.get(base + '/hello.html')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')



        resp = requests.get(base + '/404')
        self.assertEqual(resp.status_code, 404)
        self.assertEqual(resp.text, '')

        sess = requests.session()

        resp = sess.get(base + '/404')
        self.assertEqual(resp.status_code, 404)
        self.assertEqual(resp.text, '')

        resp = sess.get(base + '/404')
        self.assertEqual(resp.status_code, 404)
        self.assertEqual(resp.text, '')



        sess = requests.session()

        resp = sess.get(base + '/404')
        self.assertEqual(resp.status_code, 404)
        self.assertEqual(resp.text, '')

        resp = sess.get(base + '/hello.html')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')




        # modified since test
        resp = sess.get(base + '/hello.html')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, 'hello world')
        self.assertIn('Last-Modified', resp.headers)

        resp = sess.get(base + '/hello.html',
            headers={'If-Modified-Since': resp.headers['Last-Modified']})
        self.assertEqual(resp.status_code, 304)
        self.assertEqual(resp.text, '')



        # normalization integration test
        resp = requests.get(base + '/../../../../../../../../etc/passwd')
        self.assertEqual(resp.status_code, 404)


        # range test
        # open range
        resp = sess.get(base + '/hello.html',
            headers={'Range': 'bytes=2-'})
        self.assertEqual(resp.status_code, 206)
        self.assertEqual(resp.text, 'llo world')
        self.assertIn('Last-Modified', resp.headers)
        self.assertEqual(resp.headers['Content-Range'], 'bytes 2-10/11')
        self.assertEqual(resp.headers['Content-Length'], '9')

        # closed range
        resp = sess.get(base + '/hello.html',
            headers={'Range': 'bytes=2-3'})
        self.assertEqual(resp.status_code, 206)
        self.assertEqual(resp.text, 'll')
        self.assertIn('Last-Modified', resp.headers)
        self.assertEqual(resp.headers['Content-Range'], 'bytes 2-3/11')
        self.assertEqual(resp.headers['Content-Length'], '2')

        # > max buffer size
        resp = requests.get(base + '/hello.html', headers = {'foo': 'A' * 1024})
        self.assertEqual(resp.status_code, 400)

        # long url (under buffer size above max URL size) is 404
        resp = requests.get(base + '/' + 'a' * 800)
        self.assertEqual(resp.status_code, 404)

        # fuzz
        for _ in range(100):
            resp = requests.get(base + '/hello.html')
            self.assertEqual(resp.status_code, 200)
            self.assertEqual(resp.text, 'hello world')
