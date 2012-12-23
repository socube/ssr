/*
Copyright (c) 2012 Maarten Baert <maarten-baert@hotmail.com>

This file is part of SimpleScreenRecorder.

SimpleScreenRecorder is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SimpleScreenRecorder is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SimpleScreenRecorder.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "StdAfx.h"
#include "GLInjectLauncher.h"

#include "Logger.h"

const unsigned int GLInjectLauncher::CBUFFER_SIZE = 5;

GLInjectLauncher::GLInjectLauncher(Logger* logger, const QString& command, unsigned int max_pixels, bool run_command) {

	m_logger = logger;

	m_command = command;
	m_max_pixels = max_pixels;
	m_run_command = run_command;

	m_shm_main_id = -1;
	m_shm_main_ptr = (char*) -1;

	try {
		Init();
	} catch(...) {
		Free();
		throw;
	}

}

GLInjectLauncher::~GLInjectLauncher() {

	// free everything
	Free();

}

void GLInjectLauncher::GetCurrentSize(unsigned int* width, unsigned int* height) {
	GLInjectHeader *header = (GLInjectHeader*) m_shm_main_ptr;
	*width = header->current_width;
	*height = header->current_height;
}

void GLInjectLauncher::Init() {

	// allocate main shared memory
	m_shm_main_id = shmget(IPC_PRIVATE, sizeof(GLInjectHeader) + sizeof(GLInjectFrameInfo) * CBUFFER_SIZE, IPC_CREAT | 0777);
	if(m_shm_main_id == -1) {
		m_logger->LogError("[GLInjectLauncher::Init] Error: Can't get main shared memory!");
		throw GLInjectException();
	}
	m_shm_main_ptr = (char*) shmat(m_shm_main_id, NULL, SHM_RND);
	if(m_shm_main_ptr == (char*) -1) {
		m_logger->LogError("[GLInjectLauncher::Init] Error: Can't attach to main shared memory!");
		throw GLInjectException();
	}
	memset((void*) m_shm_main_ptr, 0, sizeof(GLInjectHeader) + sizeof(GLInjectFrameInfo) * CBUFFER_SIZE);

	// allocate frame shared memory
	for(unsigned int i = 0; i < CBUFFER_SIZE; ++i) {
		m_shm_frames.push_back(ShmFrame());
		m_shm_frames.back().id = shmget(IPC_PRIVATE, m_max_pixels * 4, IPC_CREAT | 0777);
		if(m_shm_frames.back().id == -1) {
			m_logger->LogError("[GLInjectLauncher::Init] Error: Can't get frame shared memory!");
			throw GLInjectException();
		}
		m_shm_frames.back().ptr = (char*) shmat(m_shm_frames.back().id, NULL, SHM_RND);
		if(m_shm_frames.back().ptr == (char*) -1) {
			m_logger->LogError("[GLInjectLauncher::Init] Error: Can't attach to frame shared memory!");
			throw GLInjectException();
		}
		((GLInjectFrameInfo*) (m_shm_main_ptr + sizeof(GLInjectHeader) + sizeof(GLInjectFrameInfo) * i))->shm_id = m_shm_frames.back().id;
		memset((void*) m_shm_frames.back().ptr, 0, m_max_pixels * 4);
	}

	// initialize the memory
	GLInjectHeader *header = (GLInjectHeader*) m_shm_main_ptr;
	header->cbuffer_size = CBUFFER_SIZE;
	header->max_pixels = m_max_pixels;
	header->read_pos = 0;
	header->write_pos = 0;
	header->current_width = 0;
	header->current_height = 0;

	// put the glinject libraries in the .ssr directory
	QString dir = QDir::homePath() + "/.ssr";
	if(!QDir::root().mkpath(dir)) {
		m_logger->LogError("[GLInjectLauncher::Init] Error: Can't create .ssr directory!");
		throw GLInjectException();
	}
	{
		QFile f(":/glinject/libssr-glinject32.so");
		QFile(dir + "/libssr-glinject32.so").remove();
		if(!f.copy(dir + "/libssr-glinject32.so")) {
			m_logger->LogError("[GLInjectLauncher::Init] Error: Can't save libssr-glinject32.so!");
			throw GLInjectException();
		}
	}
	{
		QFile f(":/glinject/libssr-glinject64.so");
		QFile(dir + "/libssr-glinject64.so").remove();
		if(!f.copy(dir + "/libssr-glinject64.so")) {
			m_logger->LogError("[GLInjectLauncher::Init] Error: Can't save libssr-glinject64.so!");
			throw GLInjectException();
		}
	}

	// generate the full command
	m_command = "LD_LIBRARY_PATH=\"" + dir + "\" LD_PRELOAD=\"libssr-glinject32.so libssr-glinject64.so\" SSR_GLINJECT_SHM=" + QString::number(m_shm_main_id) + " " + m_command;
	m_logger->LogInfo("[GLInjectLauncher::Init] Full command: " + m_command);

	// run it
	if(m_run_command) {
		QDir::setCurrent(QDir::homePath());
		QByteArray arg2 = m_command.toLocal8Bit();
		const char* args[] = {"/bin/sh", "-c", arg2.data(), NULL};
		pid_t pid = fork();
		if(pid == 0) {
			execl(args[0], args[0], args[1], args[2], args[3]);
		} else if(pid == -1) {
			m_logger->LogError("[GLInjectLauncher::Init] Error: Can't run command!");
			throw GLInjectException();
		}
	}

}

void GLInjectLauncher::Free() {

	// free frame shared memory
	while(!m_shm_frames.empty()) {
		if(m_shm_frames.back().ptr != (char*) -1) {
			shmdt((void*) m_shm_frames.back().ptr);
		}
		if(m_shm_frames.back().id != -1) {
			shmctl(m_shm_frames.back().id, IPC_RMID, NULL);
		}
		m_shm_frames.pop_back();
	}

	// free main shared memory
	if(m_shm_main_ptr != (char*) -1) {
		shmdt((void*) m_shm_main_ptr);
		m_shm_main_ptr = (char*) -1;
	}
	if(m_shm_main_id != -1) {
		shmctl(m_shm_main_id, IPC_RMID, NULL);
		m_shm_main_id = -1;
	}

}
