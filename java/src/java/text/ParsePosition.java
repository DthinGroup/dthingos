package java.text;

/**
 * Tracks the current position in a parsed string. In case of an error the error
 * index can be set to the position where the error occurred without having to
 * change the parse position.
 */
public class ParsePosition {

    private int currentPosition, errorIndex = -1;

    /**
     * Constructs a new {@code ParsePosition} with the specified index.
     *
     * @param index
     *            the index to begin parsing.
     */
    public ParsePosition(int index) {
        currentPosition = index;
    }

    /**
     * Compares the specified object to this {@code ParsePosition} and indicates
     * if they are equal. In order to be equal, {@code object} must be an
     * instance of {@code ParsePosition} and it must have the same index and
     * error index.
     *
     * @param object
     *            the object to compare with this object.
     * @return {@code true} if the specified object is equal to this
     *         {@code ParsePosition}; {@code false} otherwise.
     * @see #hashCode
     */
    //@Override
    public boolean equals(Object object) {
        if (!(object instanceof ParsePosition)) {
            return false;
        }
        ParsePosition pos = (ParsePosition) object;
        return currentPosition == pos.currentPosition
                && errorIndex == pos.errorIndex;
    }

    /**
     * Returns the index at which the parse could not continue.
     *
     * @return the index of the parse error or -1 if there is no error.
     */
    public int getErrorIndex() {
        return errorIndex;
    }

    /**
     * Returns the current parse position.
     *
     * @return the current position.
     */
    public int getIndex() {
        return currentPosition;
    }

    //@Override
    public int hashCode() {
        return currentPosition + errorIndex;
    }

    /**
     * Sets the index at which the parse could not continue.
     *
     * @param index
     *            the index of the parse error.
     */
    public void setErrorIndex(int index) {
        errorIndex = index;
    }

    /**
     * Sets the current parse position.
     *
     * @param index
     *            the current parse position.
     */
    public void setIndex(int index) {
        currentPosition = index;
    }

    /**
     * Returns the string representation of this parse position.
     *
     * @return the string representation of this parse position.
     */
    //@Override
    public String toString() {
        return getClass().getName() + "[index=" + currentPosition //$NON-NLS-1$
                + ", errorIndex=" + errorIndex + "]"; //$NON-NLS-1$ //$NON-NLS-2$
    }
}