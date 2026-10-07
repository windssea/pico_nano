"""真实HTTP必须经原生上传事务，授权与恢复边界。 / Actual HTTP uses native transactions with authorization/recovery boundaries."""
import pathlib,tempfile,sys,threading,urllib.request,urllib.error,json,hashlib,importlib.util
repo=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('transfer_server',repo/'tools/transfer_server.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as directory:
 root=pathlib.Path(directory)
 def start():
  app=module.TransferApp(sys.argv[1],str(root));server=module.make_server(app,'127.0.0.1',0);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();return app,server,thread
 app,server,thread=start()
 def call(path,method='GET',body=None,token=None,origin=None,extra=None):
  url='http://127.0.0.1:'+str(server.server_port);headers={'Origin':origin or url};headers.update(extra or {})
  if token:headers['Authorization']='Bearer '+token
  if isinstance(body,dict):body=json.dumps(body).encode();headers['Content-Type']='application/json'
  request=urllib.request.Request(url+path,data=body,method=method,headers=headers)
  try:
   with urllib.request.urlopen(request,timeout=30) as response:return response.status,json.load(response)
  except urllib.error.HTTPError as error:return error.code,json.load(error)
 assert call('/api/v1/status')[0]==200
 assert call('/api/v1/uploads','POST',{})[0]==401
 assert call('/api/v1/pair','POST',{'code':app.pair_code},origin='http://evil.test')[0]==403
 for i in range(5):assert call('/api/v1/pair','POST',{'code':'999999' if app.pair_code!='999999' else '000000'})[0]==401
 assert call('/api/v1/pair','POST',{'code':app.pair_code})[0]==429
 app.locked_until=0
 code,result=call('/api/v1/pair','POST',{'code':app.pair_code});assert code==200;token=result['token']
 assert call('/api/v1/uploads','POST',{},token,extra={'Host':'evil.test'})[0]==403
 expiry=app.pair_expires;app.pair_expires=0
 assert call('/api/v1/pair','POST',{'code':app.pair_code})[0]==401
 app.pair_expires=expiry
 assert call('/api/v1/uploads','POST',{'kind':'book','name':'../escape.txt','size':1,'sha256':'0'*64},token)[0]==400
 assert call('/api/v1/uploads','POST',{'kind':'book','name':'bad.txt','size':True,'sha256':'0'*64},token)[0]==400
 assert call('/api/v1/uploads','POST',{'kind':'book','name':'bad.txt','size':1,'sha256':'oops'},token)[0]==400
 assert call('/api/v1/uploads','POST',{'unknown':1},token)[0]==400
 data=('传书与续传保持原文。\n'*6000).encode();digest=hashlib.sha256(data).hexdigest()
 request={'kind':'book','name':'小说.txt','size':len(data),'sha256':digest}
 code,result=call('/api/v1/uploads','POST',request,token);assert code==201,(code,result);identifier=result['upload_id'];url='/api/v1/uploads/'+identifier
 first=data[:65536];headers={'X-Offset':'0','X-Chunk-SHA256':hashlib.sha256(first).hexdigest()}
 code,result=call(url+'/chunks','PUT',first,token,extra=headers);assert code==200 and result['next_offset']==65536,(code,result)
 assert call(url+'/chunks','PUT',first,token,extra=headers)[1]['next_offset']==65536
 old_token=token;server.shutdown();server.server_close();thread.join();app.close()
 app,server,thread=start();assert call(url,token=old_token)[0]==401
 token=call('/api/v1/pair','POST',{'code':app.pair_code})[1]['token'];code,result=call(url,token=token);assert code==200 and result['next_offset']==65536
 for at in range(65536,len(data),65536):
  block=data[at:at+65536];code,result=call(url+'/chunks','PUT',block,token,extra={'X-Offset':str(at),'X-Chunk-SHA256':hashlib.sha256(block).hexdigest()});assert code==200,(code,result)
 code,result=call(url+'/complete','POST',{'sha256':digest},token);assert code==200 and (root/'books/小说.txt').read_bytes()==data,(code,result)
 assert call('/api/v1/uploads','POST',request,token)[0]==409
 code,old=call('/api/v1/files/book?name='+urllib.parse.quote('小说.txt'),token=token);assert code==200 and old['sha256']==digest
 changed=b'new text\n';new_request={'kind':'book','name':'小说.txt','size':len(changed),'sha256':hashlib.sha256(changed).hexdigest(),'replace':{'size':len(data),'sha256':digest}}
 code,result=call('/api/v1/uploads','POST',new_request,token);assert code==201;new_url='/api/v1/uploads/'+result['upload_id'];assert call(new_url+'/chunks','PUT',changed,token,extra={'X-Offset':'0','X-Chunk-SHA256':new_request['sha256']})[0]==200
 assert call(new_url+'/complete','POST',{'sha256':new_request['sha256']},token)[0]==200 and (root/'books/小说.txt').read_bytes()==changed
 request['name']='取消.txt';code,result=call('/api/v1/uploads','POST',request,token);assert code==201;assert call('/api/v1/uploads/'+result['upload_id'],'DELETE',token=token)[0]==200 and not (root/'books/取消.txt').exists()
 for kind,name,source in [('font','测试字体.ttf',repo/'assets/fonts/read-pico-ui.ttf'),('cover','封面.png',repo/'tests/fixtures/image-black.png'),('wallpaper','锁屏.jpg',repo/'tests/fixtures/jpeg-progressive.jpg')]:
  content=source.read_bytes();sha=hashlib.sha256(content).hexdigest();code,result=call('/api/v1/uploads','POST',{'kind':kind,'name':name,'size':len(content),'sha256':sha},token);assert code==201,(code,result);resource_url='/api/v1/uploads/'+result['upload_id']
  for at in range(0,len(content),65536):
   block=content[at:at+65536];assert call(resource_url+'/chunks','PUT',block,token,extra={'X-Offset':str(at),'X-Chunk-SHA256':hashlib.sha256(block).hexdigest()})[0]==200
  assert call(resource_url+'/complete','POST',{'sha256':sha},token)[0]==200
  assert (root/{'font':'fonts','cover':'covers','wallpaper':'wallpapers'}[kind]/name).read_bytes()==content
 server.shutdown();server.server_close();thread.join();app.close()
print('HTTP transfer: native durable chunks, auth/Host/Origin, pair lockout, restart/new token, resume, explicit replacement and cancellation passed')
