#!/usr/bin/env python3

# lame ass integration test

import requests

base = 'http://localhost:10000'


def main():
    resp = requests.get(base + '/hello.html')
    assert resp.status_code == 200
    assert resp.text == 'hello world'

    sess = requests.session()

    resp = sess.get(base + '/hello.html')
    assert resp.status_code == 200
    assert resp.text == 'hello world'

    resp = sess.get(base + '/hello.html')
    assert resp.status_code == 200
    assert resp.text == 'hello world'




    resp = requests.get(base + '/404')
    assert resp.status_code == 404
    assert resp.text == ''

    sess = requests.session()

    resp = sess.get(base + '/404')
    assert resp.status_code == 404 
    assert resp.text == ''

    resp = sess.get(base + '/404')
    assert resp.status_code == 404 
    assert resp.text == ''




    sess = requests.session()

    resp = sess.get(base + '/404')
    assert resp.status_code == 404 
    assert resp.text == ''

    resp = sess.get(base + '/hello.html')
    assert resp.status_code == 200 
    assert resp.text == 'hello world'




    resp = sess.get(base + '/hello.html')
    assert resp.status_code == 200 
    assert resp.text == 'hello world'
    assert 'Last-Modified' in resp.headers

    resp = sess.get(base + '/hello.html', 
        headers={'If-Modified-Since': resp.headers['Last-Modified']})
    assert resp.status_code == 304 
    assert resp.text == ''
    
    

    resp = requests.get(base + '/../../../../../../../../etc/passwd')
    assert resp.status_code == 404



    resp = sess.get(base + '/hello.html', 
        headers={'Range': 'bytes=2-3'})
    assert resp.status_code == 206
    assert resp.text == 'll'
    assert 'Last-Modified' in resp.headers
    assert resp.headers['Content-Range'] == 'bytes 2-3/11'
    assert resp.headers['Content-Length'] == '2'

    # > max buffer size
    resp = requests.get(base + '/hello.html', headers = {'foo': 'A' * 1024})
    assert resp.status_code == 400
    
    # fuzz
    for _ in range(100):
        resp = requests.get(base + '/hello.html')
        assert resp.status_code == 200
        assert resp.text == 'hello world'
        

if __name__ == '__main__':
    main()
