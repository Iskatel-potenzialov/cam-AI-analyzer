from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from .routes.events import router
from .routes.live_state import router as live_state_router
from .routes.cameras import router as cameras_router
app=FastAPI()
app.add_middleware(CORSMiddleware,allow_origins=["http://localhost:5173","http://192.168.0.46:5173"],allow_credentials=False,allow_methods=["GET"],allow_headers=[])
@app.get("/health")
def health(): return {"status":"ok"}
app.include_router(router)
app.include_router(live_state_router)
app.include_router(cameras_router)