/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package java.io;

/**
 * An input stream that reads bytes from a file.
 *
 * <pre>
 * {@code
 *   File file = ...
 *   InputStream in = null;
 *   try {
 *     in = new BufferedInputStream(new FileInputStream(file));
 *     ...
 *   } finally {
 *     if (in != null) {
 *       in.close();
 *     }
 *   }
 * }
 * </pre>
 * <p>
 * This stream is <strong>not buffered</strong>. Most callers should wrap this stream with a
 * {@link BufferedInputStream}.
 * <p>
 * Use {@link FileReader} to read characters, as opposed to bytes, from a file.
 *
 * @see BufferedInputStream
 * @see FileOutputStream
 */
public class FileInputStream extends InputStream {

    private FileDescriptor fd;

    private native int openFile(String name);

    private native boolean closeFile(int handle);

    private native int readFile(int handle, byte[] buff, int off, int count);

    private native int available0(int handle);

    private native long skip0(int handle, long byteCount);

    /**
     * Constructs a new {@code FileInputStream} that reads from {@code file}.
     *
     * @param file the file from which this stream reads.
     * @throws FileNotFoundException if {@code file} does not exist.
     */
    public FileInputStream(File file) throws FileNotFoundException {
        if (file == null) {
            throw new NullPointerException("file == null");
        }
        String name = file.getPath();
        if (name == null) {
            throw new FileNotFoundException("FileInputStream:not found:" + name);
        }

        int ret = openFile(name);
        if (ret <= 0) {
            throw new FileNotFoundException("FileInputStream II:not found:" + name);
        }
        fd = new FileDescriptor();
        fd.handle = ret;
    }

    /**
     * Constructs a new {@code FileInputStream} that reads from {@code fd}.
     *
     * @param fd the FileDescriptor from which this stream reads.
     * @throws NullPointerException if {@code fd} is {@code null}.
     */
    public FileInputStream(FileDescriptor fd) {
        if (fd == null) {
            throw new NullPointerException("fd == null");
        }
        // TODO: to be implemented
        this.fd = fd;
    }

    /**
     * Equivalent to {@code new FileInputStream(new File(path))}.
     */
    public FileInputStream(String path) throws FileNotFoundException {
        this(new File(path));
    }

    // @Override
    public int available() throws IOException {
        int size = available0(fd.handle);
        if (size < 0) {
            throw new IOException("file available fail!");
        }
        return size;
    }

    // @Override
    public void close() throws IOException {
        boolean ret = closeFile(fd.handle);
        if (!ret) {
            throw new IOException();
        }
    }

    /**
     * Ensures that all resources for this stream are released when it is about
     * to be garbage collected.
     *
     * @throws IOException
     *         if an error occurs attempting to finalize this stream.
     */
    // @Override
    protected void finalize() throws IOException {
        if (fd != null) {
            close();
        }
    }

    /**
     * Returns the underlying file descriptor.
     */
    public final FileDescriptor getFD() throws IOException {
        return fd;
    }

    // @Override
    public int read() throws IOException {
        byte[] buffer = new byte[1];
        int ret = read(buffer, 0, 1);
        if (ret <= 0) {
            return -1;
        }
        return buffer[0];
    }

    // @Override
    public int read(byte[] buffer) throws IOException {
        return read(buffer, 0, buffer.length);
    }

    // @Override
    public int read(byte[] buffer, int byteOffset, int byteCount) throws IOException {
        int ret = readFile(fd.handle, buffer, byteOffset, byteCount);
        return ret;
    }

    // @Override
    public long skip(long byteCount) throws IOException {
        if (byteCount <= 0) {
            return 0;
        }
        return skip0(fd.handle, byteCount);
    }
}
